# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Windows launcher. Uses the installed Python/Tk and the existing Windows runner."""
from __future__ import annotations

import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import traceback
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import windows_model as model

BG, PANEL, TEXT, MUTED, GOLD, BORDER = "#111419", "#1a2028", "#ece8df", "#9aa6b5", "#d4b47b", "#333e4c"
BOOL_KEYS = {"sharpen", "jitter", "object_motion", "show_fps", "frame_generation",
             "keyboard_mouse", "mouse_capture", "mouse_invert_y", "dpad_camera"}
BOOL_KEYS.update(key for key, _, _ in model.EFFECTS)


class ScrollPage(ttk.Frame):
    def __init__(self, parent):
        super().__init__(parent)
        self.canvas = tk.Canvas(self, bg=BG, highlightthickness=0)
        bar = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=bar.set)
        self.canvas.pack(side="left", fill="both", expand=True)
        bar.pack(side="right", fill="y")
        self.content = ttk.Frame(self.canvas, padding=(26, 20, 26, 24))
        self.window = self.canvas.create_window(0, 0, window=self.content, anchor="nw")
        self.content.bind("<Configure>", lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.canvas.bind("<Configure>", lambda e: self.canvas.itemconfigure(self.window, width=e.width))
        self.content.columnconfigure(1, weight=1)


class Launcher:
    def __init__(self, root: tk.Tk, project: Path = model.ROOT):
        self.root, self.project = root, project
        self.process = None
        self.log_handle = None
        self.log_path = None
        self.stop_path = None
        self.log_offset = 0
        self.game_log_prefix = None
        self.game_log_offsets = {}
        self.active_ini = None
        self.baseline = {}
        self.inputs = []
        self.binding_widgets = {}
        self.pages = {}
        self.selected_action = "cross"
        self.ready_job = None
        self.closing = False
        self.launch_settings = model.preferences(project)
        self.launch_vars = {k: (tk.BooleanVar(value=v) if isinstance(v, bool) else tk.StringVar(value=str(v)))
                            for k, v in self.launch_settings.items()}
        self.ini_vars = {k: tk.StringVar(value=v) for k, v in model.defaults(project).items()}
        self.actions = model.binding_actions(project)
        root.title("Bloodborne • bbport Windows Launcher")
        root.configure(bg=BG)
        root.geometry(f"{min(1120, root.winfo_screenwidth()-80)}x{min(780, root.winfo_screenheight()-90)}")
        root.minsize(960, 590)
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.report_callback_exception = self.callback_error
        self.style()
        root.columnconfigure(1, weight=1)
        root.rowconfigure(1, weight=1)
        header = ttk.Frame(root, padding=(24, 18))
        header.grid(row=0, column=0, columnspan=2, sticky="ew")
        ttk.Label(header, text="BLOODBORNE", style="Brand.TLabel").pack(side="left")
        ttk.Label(header, text="bbport  /  Windows", style="Muted.TLabel").pack(side="left", padx=22)
        self.session_status = tk.StringVar(value="No active session")
        ttk.Label(header, textvariable=self.session_status, style="Gold.TLabel").pack(side="right")
        nav = ttk.Frame(root, padding=(16, 12))
        nav.grid(row=1, column=0, sticky="ns")
        body = ttk.Frame(root)
        body.grid(row=1, column=1, sticky="nsew")
        body.columnconfigure(0, weight=1)
        body.rowconfigure(0, weight=1)
        self.nav_buttons = {}
        for name, builder in (("Play", self.play_page), ("Graphics", self.graphics_page),
                              ("Game Effects", self.effects_page), ("Keyboard & Mouse", self.controls_page)):
            page = ScrollPage(body)
            page.grid(row=0, column=0, sticky="nsew")
            self.pages[name] = page
            builder(page.content)
            button = ttk.Button(nav, text=name, command=lambda n=name: self.show_page(n), width=21)
            button.pack(fill="x", pady=4)
            self.nav_buttons[name] = button
        logs = ttk.Frame(body, padding=20)
        logs.grid(row=0, column=0, sticky="nsew")
        self.pages["Session Log"] = logs
        self.logs_page(logs)
        button = ttk.Button(nav, text="Session Log", command=lambda: self.show_page("Session Log"), width=21)
        button.pack(fill="x", pady=4)
        self.nav_buttons["Session Log"] = button
        ttk.Label(nav, text="Insert  ·  Game settings\nF8  ·  Release mouse", style="Muted.TLabel",
                  justify="left").pack(side="bottom", pady=20)
        footer = ttk.Frame(root, padding=(20, 14))
        footer.grid(row=2, column=0, columnspan=2, sticky="ew")
        footer.columnconfigure(0, weight=1)
        self.status = tk.StringVar(value="Reading saved settings…")
        ttk.Label(footer, textvariable=self.status, wraplength=540, style="Muted.TLabel").grid(row=0, column=0, sticky="w")
        self.reload_button = ttk.Button(footer, text="Reload", command=self.reload)
        self.reload_button.grid(row=0, column=1, padx=5)
        self.save_button = ttk.Button(footer, text="Save settings", command=self.save)
        self.save_button.grid(row=0, column=2, padx=5)
        self.launch_button = ttk.Button(footer, text="Launch game", style="Launch.TButton", command=self.launch)
        self.launch_button.grid(row=0, column=3, padx=5)
        self.stop_button = ttk.Button(footer, text="Stop session", command=self.stop, state="disabled")
        self.stop_button.grid(row=0, column=4, padx=5)
        self.show_page("Play")
        self.load_profile(force=True)
        for var in self.launch_vars.values():
            var.trace_add("write", self.schedule_ready)
        for var in self.ini_vars.values():
            var.trace_add("write", self.schedule_ready)
        self.ready()
        root.after(350, self.poll)

    def style(self):
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure(".", background=BG, foreground=TEXT, font=("Segoe UI", 10))
        style.configure("TFrame", background=BG)
        style.configure("TLabel", background=BG)
        style.configure("Muted.TLabel", foreground=MUTED)
        style.configure("Gold.TLabel", foreground=GOLD)
        style.configure("Brand.TLabel", foreground=TEXT, font=("Georgia", 24, "bold"))
        style.configure("Title.TLabel", foreground=TEXT, font=("Segoe UI", 19, "bold"))
        style.configure("Section.TLabel", foreground=GOLD, font=("Segoe UI", 11, "bold"))
        style.configure("TButton", background=PANEL, bordercolor=BORDER, padding=(12, 9))
        style.map("TButton", background=[("active", "#2b3643"), ("disabled", PANEL)],
                  foreground=[("disabled", "#68717d")])
        style.configure("Active.TButton", background="#343d48", foreground=GOLD)
        style.configure("Launch.TButton", background="#8d343a", foreground="white", font=("Segoe UI", 11, "bold"))
        style.map("Launch.TButton", background=[("active", "#aa4249"), ("disabled", "#4d3035")])
        style.configure("TEntry", fieldbackground=PANEL, foreground=TEXT, padding=7, bordercolor=BORDER)
        style.configure("TCombobox", fieldbackground=PANEL, background=PANEL, foreground=TEXT, padding=7)
        style.map("TCombobox", fieldbackground=[("readonly", PANEL)], foreground=[("readonly", TEXT)])
        style.configure("TSpinbox", fieldbackground=PANEL, foreground=TEXT, padding=7)
        style.configure("TCheckbutton", padding=7)
        self.root.option_add("*TCombobox*Listbox.background", PANEL)
        self.root.option_add("*TCombobox*Listbox.foreground", TEXT)
        self.root.option_add("*TCombobox*Listbox.selectBackground", "#465164")
        self.root.bind_all("<MouseWheel>", self.wheel)

    def tracked(self, widget, normal="normal"):
        widget.launcher_normal_state = normal
        self.inputs.append(widget)
        return widget

    def title(self, parent, text, description):
        ttk.Label(parent, text=text, style="Title.TLabel").grid(row=0, column=0, columnspan=3, sticky="w", pady=(0, 7))
        ttk.Label(parent, text=description, style="Muted.TLabel", wraplength=700).grid(
            row=1, column=0, columnspan=3, sticky="w", pady=(0, 16))

    def section(self, parent, row, text):
        ttk.Label(parent, text=text, style="Section.TLabel").grid(
            row=row, column=0, columnspan=3, sticky="w", pady=(18, 8))

    def folder(self, parent, row, title, key):
        ttk.Label(parent, text=title).grid(row=row, column=0, sticky="w", pady=6, padx=(0, 16))
        entry = self.tracked(ttk.Entry(parent, textvariable=self.launch_vars[key]))
        entry.grid(row=row, column=1, sticky="ew", pady=6)
        def browse():
            value = filedialog.askdirectory(parent=self.root, title=title, mustexist=key in ("game", "patch", "build"))
            if value:
                self.launch_vars[key].set(value)
        self.tracked(ttk.Button(parent, text="Browse", command=browse)).grid(row=row, column=2, padx=(8, 0))

    def combo(self, parent, row, title, var, choices, callback=None):
        ttk.Label(parent, text=title).grid(row=row, column=0, sticky="w", pady=6, padx=(0, 16))
        box = self.tracked(ttk.Combobox(parent, textvariable=var, values=tuple(choices), state="readonly"), "readonly")
        box.grid(row=row, column=1, columnspan=2, sticky="ew", pady=6)
        if callback:
            box.bind("<<ComboboxSelected>>", callback)
        return box

    def mapped_combo(self, parent, row, title, variable, mapping, callback=None):
        display = tk.StringVar()
        def sync(*_):
            display.set(next((k for k, v in mapping.items() if str(v) == str(variable.get())), ""))
        def selected(event):
            variable.set(mapping[display.get()])
            if callback:
                callback(event)
        variable.trace_add("write", sync)
        sync()
        return self.combo(parent, row, title, display, mapping, selected)

    def check(self, parent, row, title, key, launch=False):
        var = (self.launch_vars if launch else self.ini_vars)[key]
        widget = ttk.Checkbutton(parent, text=title, variable=var,
                                **({} if launch else {"onvalue": "1", "offvalue": "0"}))
        self.tracked(widget).grid(row=row, column=0, columnspan=3, sticky="w")

    def spin(self, parent, row, title, variable, low, high, step):
        ttk.Label(parent, text=title).grid(row=row, column=0, sticky="w", pady=6)
        widget = self.tracked(ttk.Spinbox(parent, textvariable=variable, from_=low, to=high, increment=step, width=12))
        widget.grid(row=row, column=1, sticky="w", pady=6)

    def play_page(self, p):
        self.title(p, "The hunt begins", "Choose your game and save folder. Your dump stays untouched; launch files live in the output folder.")
        self.folder(p, 2, "Bloodborne game folder", "game")
        self.game_info = tk.StringVar()
        ttk.Label(p, textvariable=self.game_info, style="Gold.TLabel").grid(row=3, column=0, columnspan=3, sticky="w", pady=4)
        self.check(p, 4, "Use a separate official 01.09 update", "use_patch", True)
        self.folder(p, 5, "Update folder", "patch")
        self.section(p, 6, "SAVE DATA")
        self.folder(p, 7, "Port user folder", "user")
        ttk.Label(p, text="Select the folder containing savedata and cache, not SPRJ0005 itself.",
                  style="Muted.TLabel", wraplength=680).grid(row=8, column=0, columnspan=3, sticky="w")
        actions = ttk.Frame(p)
        actions.grid(row=9, column=0, columnspan=3, sticky="w", pady=10)
        self.tracked(ttk.Button(actions, text="Import shadPS4 save…", command=self.import_save)).pack(side="left")
        self.tracked(ttk.Button(actions, text="Open user folder", command=lambda: self.open_folder("user"))).pack(side="left", padx=8)
        self.section(p, 10, "PROFILE & EXECUTABLES")
        self.folder(p, 11, "Settings / output folder", "output")
        self.tracked(ttk.Button(p, text="Load selected profile", command=self.load_profile)).grid(row=12, column=1, sticky="w", pady=5)
        self.folder(p, 13, "Executable folder", "build")
        ttk.Label(p, text="Settings are shared with Insert. Change the FG runtime in Graphics. "
                  "Close the game before importing saves or editing launch settings.",
                  style="Muted.TLabel", wraplength=700).grid(row=14, column=0, columnspan=3, sticky="w", pady=14)

    def graphics_page(self, p):
        self.title(p, "Graphics", "Output resolution and window resolution are separate. FG uses the actual window / swapchain size.")
        self.mapped_combo(p, 2, "Upscaler", self.ini_vars["upscaler"], model.UPSCALERS)
        self.mapped_combo(p, 3, "Preset", self.ini_vars["preset"], {n: str(i) for i, n in enumerate(model.PRESETS)})
        self.combo(p, 4, "Output resolution", self.ini_vars["output_res"], model.RESOLUTIONS)
        self.combo(p, 5, "Window resolution", self.launch_vars["window"], model.RESOLUTIONS)
        self.mapped_combo(p, 6, "Resolution changes", self.ini_vars["live_resolution"],
                          {"Startup (restart required)": "0", "Live": "1", "Auto": "auto"})
        self.mapped_combo(p, 7, "Window mode (F11)", self.ini_vars["window_mode"], model.WINDOW_MODES)
        self.section(p, 8, "FRAME GENERATION")
        self.mapped_combo(p, 9, "FG runtime", self.launch_vars["engine"], model.ENGINES, self.engine_changed)
        self.check(p, 10, "Enable Frame Generation", "frame_generation")
        self.mapped_combo(p, 11, "FG multiplier", self.ini_vars["fg_multiplier"], {f"{i}X": str(i) for i in range(2, 7)})
        ttk.Label(p, text="Experimental. SM86 provides 2X–6X on the tested RTX 3050 setup. "
                  "The in-game menu enforces the runtime's reported limit. Generated FPS is separate "
                  "from real gameplay FPS; keep the game focused.", style="Muted.TLabel", wraplength=700).grid(
                      row=12, column=0, columnspan=3, sticky="w", pady=8)
        self.section(p, 13, "PACING")
        self.mapped_combo(p, 14, "Presentation", self.launch_vars["present"],
                          {"Immediate (allow tearing)": "Immediate", "Mailbox": "Mailbox", "FIFO (VSync)": "Fifo"})
        self.mapped_combo(p, 15, "Game FPS patch", self.launch_vars["fps_patch"],
                          {"Uncapped": "uncap", "60 FPS": "60", "90 FPS": "90", "Preserve game patch": "preserve"})
        self.spin(p, 16, "FPS limit (0 = unlimited)", self.launch_vars["fps_limit"], 0, 480, 1)
        self.check(p, 17, "Show FPS counter", "show_fps")
        self.section(p, 18, "RECONSTRUCTION")
        self.check(p, 19, "Sharpen image", "sharpen")
        self.spin(p, 20, "Sharpness", self.ini_vars["sharpness"], 0, 2, .05)
        self.check(p, 21, "Subpixel jitter", "jitter")
        self.check(p, 22, "Character motion vectors", "object_motion")

    def effects_page(self, p):
        self.title(p, "Game effects", "Applied when the runner prepares the game. Restart after changing these during a session in Insert.")
        for row, (key, label, _) in enumerate(model.EFFECTS, 2):
            self.check(p, row, label, key)
        self.mapped_combo(p, 13, "Model detail", self.ini_vars["model_lod"],
                          {"Highest": "-2", "High": "-1", "Game default": "0", "Lower": "1", "Lowest": "2"})

    def controls_page(self, p):
        self.title(p, "Keyboard & mouse", "Click a controller button to find its bindings. Choose a key or capture an input; Insert and F8 stay reserved.")
        self.check(p, 2, "Enable keyboard and mouse", "keyboard_mouse")
        self.check(p, 3, "Capture mouse for camera control", "mouse_capture")
        self.check(p, 4, "Invert mouse Y", "mouse_invert_y")
        self.spin(p, 5, "Mouse sensitivity", self.ini_vars["mouse_sensitivity"], .1, 5, .1)
        self.check(p, 6, "D-pad controls camera", "dpad_camera")
        ttk.Label(p, text="Leave D-pad camera OFF to keep item/menu actions. WASD moves; mouse / arrow keys turn the camera.",
                  style="Muted.TLabel", wraplength=700).grid(row=7, column=0, columnspan=3, sticky="w", pady=6)
        self.controller = tk.Canvas(p, width=600, height=240, background=PANEL, highlightthickness=1, highlightbackground=BORDER)
        self.controller.grid(row=8, column=0, columnspan=3, sticky="ew", pady=12)
        self.controller.bind("<Configure>", lambda e: self.draw_controller())
        self.binding_detail = tk.StringVar()
        ttk.Label(p, textvariable=self.binding_detail, style="Gold.TLabel").grid(
            row=9, column=0, columnspan=3, sticky="w", pady=8)
        table = ttk.Frame(p)
        table.grid(row=10, column=0, columnspan=3, sticky="ew")
        table.columnconfigure(1, weight=1)
        table.columnconfigure(3, weight=1)
        for col, title in ((0, "Controller / action"), (1, "Primary"), (3, "Secondary")):
            ttk.Label(table, text=title, style="Section.TLabel").grid(row=0, column=col, sticky="w", pady=10)
        self.action_rows = {}
        for row, action in enumerate(self.actions, 1):
            label = ttk.Label(table, text=f'{action["label"]}\n{action["action"]}', wraplength=180)
            label.grid(row=row, column=0, sticky="w", pady=5, padx=(0, 12))
            self.action_rows[action["key"]] = label
            for col, suffix in ((1, ""), (3, "_alt")):
                key = f'bind_{action["key"]}{suffix}'
                display = tk.StringVar()
                box = self.tracked(ttk.Combobox(table, textvariable=display, state="readonly", width=15), "readonly")
                box.grid(row=row, column=col, sticky="ew", pady=5)
                box.bind("<<ComboboxSelected>>", lambda e, k=key, v=display: self.assign(k, v.get()))
                self.binding_widgets[key] = (display, box)
                self.tracked(ttk.Button(table, text="Capture", command=lambda k=key: self.capture(k))).grid(
                    row=row, column=col+1, padx=(5, 12), pady=5)
        self.tracked(ttk.Button(p, text="Reset bindings to defaults", command=self.reset_bindings)).grid(
            row=11, column=0, columnspan=3, sticky="w", pady=16)

    def logs_page(self, p):
        p.columnconfigure(0, weight=1)
        p.rowconfigure(2, weight=1)
        ttk.Label(p, text="Session log", style="Title.TLabel").grid(row=0, column=0, sticky="w", pady=(0, 8))
        ttk.Label(p, text="Save in the game menu before Stop session; Stop ends the process without an additional save.",
                  style="Muted.TLabel", wraplength=700).grid(row=4, column=0, sticky="w", pady=6)
        self.log_label = tk.StringVar(value="Launch output will appear here.")
        ttk.Label(p, textvariable=self.log_label, style="Muted.TLabel", wraplength=760).grid(row=1, column=0, sticky="w", pady=8)
        self.log_text = tk.Text(p, bg="#0b0e12", fg=TEXT, insertbackground=TEXT, relief="flat",
                                font=("Consolas", 9), wrap="word", state="disabled")
        self.log_text.grid(row=2, column=0, sticky="nsew")
        bar = ttk.Scrollbar(p, command=self.log_text.yview)
        bar.grid(row=2, column=1, sticky="ns")
        self.log_text.configure(yscrollcommand=bar.set)
        ttk.Button(p, text="Open log folder", command=self.open_logs).grid(row=3, column=0, sticky="w", pady=12)

    def show_page(self, name):
        self.current_page = name
        self.pages[name].tkraise()
        for title, button in self.nav_buttons.items():
            button.configure(style="Active.TButton" if title == name else "TButton")

    def wheel(self, event):
        page = self.pages.get(self.current_page)
        if isinstance(page, ScrollPage) and event.widget.winfo_class() not in ("Text", "TCombobox", "TSpinbox"):
            page.canvas.yview_scroll(-int(event.delta / 120), "units")

    def draw_controller(self):
        c = self.controller
        c.delete("all")
        scale = max(1, c.winfo_width()) / 600
        height = round(240 * scale)
        if c.winfo_height() != height:
            c.configure(height=height)
        def polygon(points, **kw):
            return c.create_polygon(*[v*scale for v in points], **kw)
        polygon((92,65,160,36,440,36,508,65,546,207,473,219,402,150,198,150,127,219,54,207),
                fill="#29313c", outline="#5c6878", width=2)
        def button(key, label, x, y, w=42, h=30, color=TEXT):
            chosen = key == self.selected_action
            tag = f"control_{key}"
            c.create_rectangle((x-w/2)*scale, (y-h/2)*scale, (x+w/2)*scale, (y+h/2)*scale,
                               fill="#4c4232" if chosen else "#121820", outline=GOLD if chosen else BORDER, tags=tag)
            c.create_text(x*scale, y*scale, text=label, fill=color, font=("Segoe UI", 10, "bold"), tags=tag)
            c.tag_bind(tag, "<Button-1>", lambda e, k=key: self.select_action(k))
            c.tag_bind(tag, "<Enter>", lambda e: c.configure(cursor="hand2"))
            c.tag_bind(tag, "<Leave>", lambda e: c.configure(cursor=""))
        button("l2", "L2", 140, 16, 65)
        button("r2", "R2", 460, 16, 65)
        button("l1", "L1", 140, 49, 65)
        button("r1", "R1", 460, 49, 65)
        button("dpad_up", "↑", 153, 90, 27)
        button("dpad_down", "↓", 153, 144, 27)
        button("dpad_left", "←", 126, 117, 27)
        button("dpad_right", "→", 180, 117, 27)
        button("triangle", "△", 447, 90, 30, color="#92ceb2")
        button("cross", "×", 447, 144, 30, color="#8fb6eb")
        button("square", "□", 420, 117, 30, color="#c797c9")
        button("circle", "○", 474, 117, 30, color="#e39393")
        button("l3", "L3", 230, 160, 48, 40)
        button("r3", "R3", 370, 160, 48, 40)
        button("touchpad_left", "Touch L", 269, 85, 60, 44)
        button("touchpad_right", "Touch R", 331, 85, 60, 44)
        button("options", "Options", 380, 75, 51, 25)
        c.create_text(300*scale, 219*scale, text="CLICK A BUTTON TO EDIT ITS KEYS", fill=MUTED, font=("Segoe UI", 9))
        self.update_detail()

    def select_action(self, key):
        self.selected_action = key
        self.draw_controller()
        for action, label in self.action_rows.items():
            label.configure(foreground=GOLD if action == key else TEXT)
        page = self.pages["Keyboard & Mouse"]
        label = self.action_rows[key]
        # Move the row into view while preserving the diagram as a navigation aid above it.
        page.canvas.yview_moveto(max(0, (label.winfo_y() + label.master.winfo_y()-150) / max(1, page.content.winfo_height())))
        self.binding_widgets[f"bind_{key}"][1].focus_set()

    def update_detail(self):
        if not hasattr(self, "binding_detail"):
            return
        action = next(a for a in self.actions if a["key"] == self.selected_action)
        names = [model.binding_name(int(self.ini_vars[f'bind_{action["key"]}{s}'].get())) for s in ("", "_alt")]
        self.binding_detail.set(f'{action["label"]}  ·  {action["action"]}  ·  {" / ".join(names)}')

    def binding_names(self):
        for key, (display, box) in self.binding_widgets.items():
            name = model.binding_name(int(self.ini_vars[key].get()))
            box.configure(values=tuple(model.BINDING_CHOICES) + (() if name in model.BINDING_CHOICES else (name,)))
            display.set(name)
        self.update_detail()

    def assign(self, key, name):
        code = model.BINDING_CHOICES.get(name)
        if code is None and name.startswith("Scancode "):
            code = int(name.split()[1])
        values = self.values()
        model.rebind(values, key, code)
        for k, v in values.items():
            if k.startswith("bind_"):
                self.ini_vars[k].set(v)
        self.selected_action = key.removeprefix("bind_").removesuffix("_alt")
        self.binding_names()
        self.draw_controller()

    def capture(self, key):
        popup = tk.Toplevel(self.root)
        popup.title("Capture binding")
        popup.configure(bg=BG)
        popup.transient(self.root)
        popup.resizable(False, False)
        ttk.Label(popup, text="Press a key or mouse button", style="Section.TLabel", padding=24).pack()
        status = tk.StringVar(value="Escape cancels. Choose Escape from the list to bind it.")
        ttk.Label(popup, textvariable=status, wraplength=420, padding=(24, 0, 24, 24)).pack()
        popup.grab_set()
        popup.focus_set()
        def apply(code):
            if code is None:
                status.set("Unsupported key. Choose a named input from the list instead.")
            elif code in (65, 73):
                status.set("F8 and Insert are reserved for the game window.")
            else:
                self.assign(key, model.binding_name(code))
                popup.destroy()
        def keyboard(event):
            if event.keysym == "Escape":
                popup.destroy()
            else:
                apply(model.key_event_code(event.keysym, event.keycode))
            return "break"
        popup.bind("<KeyPress>", keyboard)
        popup.bind("<ButtonPress>", lambda e: apply(-e.num) if 1 <= e.num <= 5 else None)

    def reset_bindings(self):
        for key, value in model.defaults(self.project).items():
            if key.startswith("bind_"):
                self.ini_vars[key].set(value)
        self.binding_names()
        self.draw_controller()

    def values(self):
        return {k: v.get() for k, v in self.ini_vars.items()}

    def settings(self):
        values = {k: v.get() for k, v in self.launch_vars.items()}
        values["fps_limit"] = int(values["fps_limit"])
        return values

    def engine_changed(self, _=None):
        mode = self.launch_vars["engine"].get()
        previous_auto = {str(self.project / "out/windows"), str(self.project / "out/windows-fg-game")}
        if self.launch_vars["build"].get() in previous_auto:
            self.launch_vars["build"].set(str(self.project / ("out/windows" if mode == "off" else "out/windows-fg-game")))
        if mode != "off":
            self.launch_vars["present"].set("Immediate")

    def load_profile(self, force=False):
        if not force and self.active_ini and any(
                self.ini_vars[k].get() != self.loaded_values.get(k) for k in self.ini_vars):
            if not messagebox.askyesno("Switch settings profile", "Discard unsaved edits and load the selected profile?", parent=self.root):
                return
        path = model.resolve(self.project, self.launch_vars["output"].get()) / "bbport.ini"
        disk = model.read_ini(path)
        loaded = {**model.defaults(self.project), **disk}
        if "window_mode" not in disk and self.launch_vars["fullscreen"].get():
            loaded["window_mode"] = "borderless"
        self.launch_vars["fullscreen"].set(False)
        for key, variable in self.ini_vars.items():
            variable.set(loaded[key])
        self.active_ini = path
        self.baseline = disk
        self.loaded_values = self.values()
        self.binding_names()
        self.draw_controller()
        if hasattr(self, "status"):
            self.status.set(f"Loaded {path}")

    def reload(self):
        if self.process:
            return
        self.load_profile()
        self.ready()

    def save(self, show_status=True):
        try:
            settings, values = self.settings(), self.values()
            model.validate(self.project, settings, values, launching=False)
            selected = model.resolve(self.project, settings["output"]) / "bbport.ini"
            if selected != self.active_ini:
                raise ValueError("Click Load selected profile before saving to a different output folder.")
            disk = model.save_ini(selected, values, self.baseline, self.loaded_values)
            model.atomic_write(self.project / "out/windows-launcher/settings.json", json.dumps(settings, indent=2) + "\n")
            self.load_profile(force=True)
            if show_status:
                self.status.set("Settings saved. Insert uses this same profile.")
            return True
        except (OSError, ValueError, TypeError) as error:
            messagebox.showerror("Cannot save settings", str(error), parent=self.root)
            return False

    def schedule_ready(self, *_):
        if self.ready_job:
            self.root.after_cancel(self.ready_job)
        self.ready_job = self.root.after(250, self.ready)

    def ready(self):
        self.ready_job = None
        if self.process:
            return
        try:
            settings = self.settings()
            model.validate(self.project, settings, self.values())
            info = model.metadata(model.resolve(self.project, settings["game"]))
            self.game_info.set(f'{info.get("TITLE_ID", "")}  ·  Game version {info.get("APP_VER", "")}')
            self.status.set("Ready to launch · settings shared with Insert")
        except (OSError, ValueError, TypeError, KeyError) as error:
            self.status.set(str(error))
            self.game_info.set("")

    def launch(self):
        if self.process:
            return
        try:
            settings = self.settings()
            model.validate(self.project, settings, self.values())
            if not self.save(show_status=False):
                return
            log_dir = model.resolve(self.project, settings["output"]) / "logs"
            log_dir.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
            self.log_path = log_dir / f"launcher-{stamp}.log"
            self.stop_path = log_dir / f"stop-{stamp}.trigger"
            self.game_log_prefix = log_dir / f"game-{stamp}"
            self.game_log_offsets = {}
            command = model.launch_command(self.project, settings, game_log_prefix=self.game_log_prefix)
            self.log_handle = self.log_path.open("wb")
            self.process = subprocess.Popen(command, cwd=self.project,
                                            env=model.launch_environment(settings, self.stop_path),
                                            stdout=self.log_handle, stderr=subprocess.STDOUT,
                                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            self.log_offset = 0
            self.log_text.configure(state="normal")
            self.log_text.delete("1.0", "end")
            self.log_text.configure(state="disabled")
            self.log_label.set(f"Preparation: {self.log_path}\nGame: {self.game_log_prefix.name}-*.log (same folder)")
            self.session_status.set("SESSION RUNNING")
            self.status.set("Preparing game files… game window will open automatically.")
            self.set_running(True)
        except (OSError, ValueError, TypeError) as error:
            if self.log_handle:
                self.log_handle.close()
                self.log_handle = None
            messagebox.showerror("Cannot launch game", str(error), parent=self.root)

    def set_running(self, running):
        for widget in self.inputs:
            widget.configure(state="disabled" if running else widget.launcher_normal_state)
        for widget in (self.launch_button, self.save_button, self.reload_button):
            widget.configure(state="disabled" if running else "normal")
        self.stop_button.configure(state="normal" if running else "disabled")

    def append_log(self, text):
        if not text:
            return
        self.log_text.configure(state="normal")
        self.log_text.insert("end", text)
        if int(self.log_text.index("end-1c").split(".")[0]) > 5000:
            self.log_text.delete("1.0", "1000.0")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def poll(self):
        if self.closing:
            return
        if self.process:
            if self.log_path:
                with self.log_path.open("rb") as stream:
                    stream.seek(self.log_offset)
                    data = stream.read(128 * 1024)
                    self.log_offset = stream.tell()
                self.append_log(data.decode("utf-8", "replace"))
            for path in sorted(self.game_log_prefix.parent.glob(self.game_log_prefix.name + "-*.log")):
                offset = self.game_log_offsets.get(path, 0)
                with path.open("rb") as stream:
                    stream.seek(offset)
                    data = stream.read(128 * 1024)
                    self.game_log_offsets[path] = stream.tell()
                self.append_log(data.decode("utf-8", "replace"))
            code = self.process.poll()
            if code is not None:
                self.log_handle.close()
                self.log_handle = None
                self.process = None
                self.set_running(False)
                self.load_profile(force=True)
                self.session_status.set("No active session")
                self.status.set(f"Session ended (exit {code}). Insert settings reloaded.")
                self.stop_path.unlink(missing_ok=True)
                if code not in (0,):
                    self.show_page("Session Log")
        self.root.after(350, self.poll)

    def stop(self):
        if self.process and self.stop_path:
            self.stop_path.write_text("stop\n", encoding="ascii")
            self.status.set("Stop requested. Waiting for the game runner…")
            self.stop_button.configure(state="disabled")

    def import_save(self):
        source = filedialog.askdirectory(parent=self.root, title="Select decrypted SPRJ0005 save")
        if not source:
            return
        try:
            settings = self.settings()
            model.validate(self.project, settings, self.values(), launching=False)
            user = model.resolve(self.project, settings["user"])
            if not messagebox.askyesno("Import save", f"Copy {source}\n\ninto {user}\\savedata\\1?\n"
                                       "The source is kept. An existing target receives a backup.", parent=self.root):
                return
            target, backup = model.import_save(Path(source), user)
            messagebox.showinfo("Save imported", f"Save: {target}" + (f"\nBackup: {backup}" if backup else ""), parent=self.root)
        except (OSError, ValueError) as error:
            messagebox.showerror("Cannot import save", str(error), parent=self.root)

    def open_folder(self, key):
        path = model.resolve(self.project, self.launch_vars[key].get())
        path.mkdir(parents=True, exist_ok=True)
        os.startfile(path)

    def open_logs(self):
        if self.log_path:
            os.startfile(self.log_path.parent)
        else:
            self.open_folder("output")

    def callback_error(self, kind, value, trace):
        path = self.project / "out/windows-launcher/launcher-error.log"
        model.atomic_write(path, "".join(traceback.format_exception(kind, value, trace)))
        messagebox.showerror("Launcher error", f"{value}\n\nDetails: {path}", parent=self.root)

    def close(self):
        if self.process and not messagebox.askyesno(
                "Game is running", "Close the launcher and leave the game running?", parent=self.root):
            return
        self.closing = True
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="Construct and check all pages without launching the game")
    args = parser.parse_args()
    root = tk.Tk()
    app = Launcher(root)
    if args.self_test:
        for name in app.pages:
            app.show_page(name)
            root.update()
            page = app.pages[name]
            assert page.winfo_width() > 500 and page.winfo_height() > 350, (name, page.winfo_geometry())
        assert len(app.binding_widgets) == 34
        assert len(app.controller.find_all()) >= 30
        root.geometry("960x590")
        app.show_page("Keyboard & Mouse")
        root.update()
        root.update()
        bounds = app.controller.bbox("all")
        assert bounds[0] >= 0 and bounds[2] <= app.controller.winfo_width(), bounds
        assert bounds[1] >= -1 and bounds[3] <= app.controller.winfo_height(), bounds
        assert model.resolve(app.project, app.launch_vars["output"].get()) / "bbport.ini" == app.active_ini
        print("UI self-test passed: 5 pages, 17 controller actions, 34 binding slots, shared profile.")
        app.closing = True
        root.destroy()
    else:
        root.mainloop()


if __name__ == "__main__":
    try:
        main()
    except Exception:
        error_path = model.ROOT / "out/windows-launcher/startup-error.log"
        model.atomic_write(error_path, traceback.format_exc())
        if "--self-test" in sys.argv:
            raise
        messagebox.showerror("Cannot open bbport launcher", f"See {error_path}\n\n{traceback.format_exc().splitlines()[-1]}")
