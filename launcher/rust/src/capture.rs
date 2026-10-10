// SPDX-License-Identifier: GPL-2.0-or-later
// Only sampled while the launcher owns focus and a binding capture is active.
#[cfg(windows)]
#[link(name = "user32")]
unsafe extern "system" {
    fn GetAsyncKeyState(key: i32) -> i16;
    fn MapVirtualKeyW(key: u32, map_type: u32) -> u32;
}
pub fn pressed() -> Vec<i32> {
    #[cfg(windows)]
    {
        (1..=254)
            .filter(|&k| unsafe { GetAsyncKeyState(k) } < 0)
            .collect()
    }
    #[cfg(not(windows))]
    {
        Vec::new()
    }
}
pub fn code(vk: i32) -> Option<i32> {
    Some(match vk {
        1 => -1,
        2 => -3,
        4 => -2,
        5 => -4,
        6 => -5,
        65..=90 => vk - 65 + 4,
        49..=57 => vk - 49 + 30,
        48 => 39,
        96..=105 => {
            if vk == 96 {
                98
            } else {
                vk - 96 + 88
            }
        }
        112..=123 => vk - 112 + 58,
        8 => 42,
        9 => 43,
        13 => 40,
        27 => 41,
        32 => 44,
        45 => 73,
        46 => 76,
        36 => 74,
        35 => 77,
        33 => 75,
        34 => 78,
        37 => 80,
        38 => 82,
        39 => 79,
        40 => 81,
        160 => 225,
        161 => 229,
        162 => 224,
        163 => 228,
        164 => 226,
        165 => 230,
        91 => 227,
        92 => 231,
        20 => 57,
        44 => 70,
        145 => 71,
        19 => 72,
        144 => 83,
        111 => 84,
        106 => 85,
        109 => 86,
        107 => 87,
        110 => 99,
        93 => 101,
        186 => 51,
        187 => 46,
        188 => 54,
        189 => 45,
        190 => 55,
        191 => 56,
        192 => 53,
        219 => 47,
        220 => 49,
        221 => 48,
        222 => 52,
        _ => return None,
    })
}
pub fn physical_code(vk: i32) -> Option<i32> {
    // Letter/number/OEM virtual keys depend on the active keyboard layout.
    // Translate their physical set-1 position to the same USB HID ID SDL uses.
    #[cfg(windows)]
    if (48..=90).contains(&vk) || (186..=222).contains(&vk) {
        let scan = unsafe { MapVirtualKeyW(vk as u32, 4) };
        let code = match scan {
            0x02..=0x0a => scan as i32 + 28,
            0x0b => 39,
            0x0c => 45,
            0x0d => 46,
            0x10 => 20,
            0x11 => 26,
            0x12 => 8,
            0x13 => 21,
            0x14 => 23,
            0x15 => 28,
            0x16 => 24,
            0x17 => 12,
            0x18 => 18,
            0x19 => 19,
            0x1a => 47,
            0x1b => 48,
            0x1e => 4,
            0x1f => 22,
            0x20 => 7,
            0x21 => 9,
            0x22 => 10,
            0x23 => 11,
            0x24 => 13,
            0x25 => 14,
            0x26 => 15,
            0x27 => 51,
            0x28 => 52,
            0x29 => 53,
            0x2b => 49,
            0x2c => 29,
            0x2d => 27,
            0x2e => 6,
            0x2f => 25,
            0x30 => 5,
            0x31 => 17,
            0x32 => 16,
            0x33 => 54,
            0x34 => 55,
            0x35 => 56,
            0x56 => 100,
            _ => return code(vk),
        };
        return Some(code);
    }
    code(vk)
}
