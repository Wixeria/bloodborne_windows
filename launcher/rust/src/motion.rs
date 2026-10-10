// SPDX-License-Identifier: GPL-2.0-or-later
//! Short, event-driven motion. Native components keep their focus and disabled behavior.
use gpui_kit::base::{Presence, Transition, transition};
use gpui_kit::component::{
    ActiveTheme,
    button::{Button, ButtonCustomVariant, ButtonVariants},
};
use gpui_kit::*;
use std::time::Duration;

pub fn quick() -> Transition {
    Transition::new(Duration::from_millis(150))
}

#[derive(Default)]
struct Interaction {
    hovered: bool,
    pressed: bool,
}

pub struct Surface {
    id: ElementId,
    inner: Div,
    delay: u64,
}
impl Surface {
    pub fn new(id: impl Into<ElementId>, inner: Div, delay: u64) -> Self {
        Self {
            id: id.into(),
            inner,
            delay,
        }
    }
}
impl ParentElement for Surface {
    fn extend(&mut self, elements: impl IntoIterator<Item = AnyElement>) {
        self.inner.extend(elements);
    }
}
impl IntoElement for Surface {
    type Element = ViewElement<Self>;
    fn into_element(self) -> Self::Element {
        ViewElement::new(self)
    }
}
impl RenderOnce for Surface {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let state = window.use_keyed_state((self.id.clone(), "interaction"), cx, |_, _| {
            Interaction::default()
        });
        let hovered = state.read(cx).hovered;
        let border = transition(
            (self.id.clone(), "border"),
            Hsla::from(rgb(if hovered { 0x64573f } else { 0x303740 })),
            quick(),
            window,
            cx,
        );
        let background = transition(
            (self.id.clone(), "background"),
            Hsla::from(rgb(if hovered { 0x1c232b } else { 0x171d25 })),
            quick(),
            window,
            cx,
        );
        let enter = Presence::new((self.id.clone(), "enter"), true)
            .transition(
                Transition::new(Duration::from_millis(260))
                    .delay(Duration::from_millis(self.delay)),
            )
            .sample(window, cx)
            .progress;
        self.inner
            .id(self.id)
            .relative()
            .top(px(4. * (1. - enter)))
            .opacity(0.82 + 0.18 * enter)
            .border_color(border)
            .bg(background)
            .on_hover(move |hovered, window, cx| {
                state.update(cx, |state, _| state.hovered = *hovered);
                window.refresh();
            })
    }
}

#[derive(Clone, Copy)]
pub enum ButtonKind {
    Primary,
    Outline,
    Ghost,
    Danger,
}
pub struct AnimatedButton {
    id: ElementId,
    button: Button,
    kind: ButtonKind,
    enabled: bool,
}
pub trait ButtonMotionExt {
    fn animated(self, id: impl Into<ElementId>, kind: ButtonKind, enabled: bool) -> AnimatedButton;
}
impl ButtonMotionExt for Button {
    fn animated(self, id: impl Into<ElementId>, kind: ButtonKind, enabled: bool) -> AnimatedButton {
        AnimatedButton {
            id: id.into(),
            button: self,
            kind,
            enabled,
        }
    }
}
impl IntoElement for AnimatedButton {
    type Element = ViewElement<Self>;
    fn into_element(self) -> Self::Element {
        ViewElement::new(self)
    }
}
impl RenderOnce for AnimatedButton {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let state = window.use_keyed_state((self.id.clone(), "interaction"), cx, |_, _| {
            Interaction::default()
        });
        let hovered = self.enabled && state.read(cx).hovered;
        let pressed = self.enabled && state.read(cx).pressed;
        let (normal, hover, active) = match self.kind {
            ButtonKind::Primary => (
                rgb(0xcbb082).into(),
                rgb(0xe2cda8).into(),
                rgb(0xb69a6c).into(),
            ),
            ButtonKind::Outline => (
                rgb(0x48515d).into(),
                rgb(0x8b7655).into(),
                rgb(0xb49769).into(),
            ),
            ButtonKind::Ghost => (
                hsla(0., 0., 0., 0.),
                rgba(0xb7c4d91b).into(),
                rgba(0xcbb08230).into(),
            ),
            ButtonKind::Danger => (
                rgb(0x9e4349).into(),
                rgb(0xb85a60).into(),
                rgb(0x84363c).into(),
            ),
        };
        let color = transition(
            (self.id.clone(), "color"),
            if !self.enabled {
                normal.opacity(0.15)
            } else if pressed {
                active
            } else if hovered {
                hover
            } else {
                normal
            },
            quick(),
            window,
            cx,
        );
        let press = transition(
            (self.id.clone(), "press"),
            if pressed { 0.95_f32 } else { 1. },
            quick(),
            window,
            cx,
        );
        let background = match self.kind {
            ButtonKind::Outline => transition(
                (self.id.clone(), "outline-background"),
                Hsla::from(if hovered {
                    rgba(0xcbb08212)
                } else {
                    rgba(0xcbb08200)
                }),
                quick(),
                window,
                cx,
            ),
            _ => color,
        };
        let foreground = match self.kind {
            ButtonKind::Primary => rgb(0x16191c).into(),
            ButtonKind::Danger => rgb(0xffeeee).into(),
            _ => cx.theme().foreground,
        };
        // Feed the interpolated color into all native pointer states. Explicit
        // background avoids Custom's translucent normal tint on primary buttons.
        let button = self
            .button
            .custom(
                ButtonCustomVariant::new(cx)
                    .color(color)
                    .hover(background)
                    .active(background)
                    .foreground(foreground),
            )
            .bg(background)
            .border_color(color);
        let down = state.clone();
        let up = state.clone();
        let out = state.clone();
        div()
            .id(self.id)
            .opacity(press)
            .on_hover(move |hovered, window, cx| {
                state.update(cx, |state, _| {
                    state.hovered = *hovered;
                    if !hovered {
                        state.pressed = false;
                    }
                });
                window.refresh();
            })
            .on_mouse_down(MouseButton::Left, move |_, window, cx| {
                down.update(cx, |state, _| state.pressed = true);
                window.refresh();
            })
            .on_mouse_up(MouseButton::Left, move |_, window, cx| {
                up.update(cx, |state, _| state.pressed = false);
                window.refresh();
            })
            .on_mouse_up_out(MouseButton::Left, move |_, window, cx| {
                out.update(cx, |state, _| state.pressed = false);
                window.refresh();
            })
            .child(button)
    }
}
