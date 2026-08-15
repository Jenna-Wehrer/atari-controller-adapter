#include "tusb.h"

// =====================================================================
// DEVICE DESCRIPTOR
// =====================================================================
// This is the first thing the host asks for when you plug in.
// It answers: "Who are you, what USB version do you speak, and
// how should I identify you?"
//
// About half the values here are fixed by the USB spec — you'd
// see the same numbers in any HID device. The rest are your choices.
// =====================================================================

static uint8_t const desc_device[] = {
    // --- Fixed by spec: structure ---

    0x12,                // bLength: 18
                         //   Device descriptors are ALWAYS 18 bytes.
                         //   This is in the USB spec — not negotiable.

    0x01,                // bDescriptorType: DEVICE
                         //   0x01 always means "this is a device descriptor."
                         //   Every descriptor type has a fixed number:
                         //     0x01 = device, 0x02 = config, 0x04 = interface,
                         //     0x05 = endpoint, 0x21 = HID, 0x22 = report, etc.
                         //   These are assigned by the USB spec, not you.

    // --- Your choice: USB version ---

    0x10, 0x01,          // bcdUSB: 1.10
                         //   What USB version this device speaks.
                         //   Written in BCD (binary-coded decimal), little-endian.
                         //   The RP2040's USB controller is USB 1.1 hardware,
                         //   so 1.10 is the honest answer. You could say 2.0
                         //   and most hosts wouldn't care, but 1.10 is truthful.

    // --- Fixed by spec: class deferred to interface ---

    0x00,                // bDeviceClass: 0 (defer to interface)
                         //   We could set this to 0x03 (HID) here, but
                         //   convention says to leave it 0 and declare
                         //   the class in the interface descriptor instead.
                         //   Either works; 0 is the traditional approach.

    0x00,                // bDeviceSubClass: 0
                         //   Only meaningful if bDeviceClass is nonzero.
                         //   Since we set class = 0, this is irrelevant.

    0x00,                // bDeviceProtocol: 0
                         //   Same — only meaningful if bDeviceClass is nonzero.

    // --- Constrained choice: must be 8, 16, 32, or 64 ---

    CFG_TUD_ENDPOINT0_SIZE,  // bMaxPacketSize0: 64
                             //   Endpoint 0 is the control endpoint — always
                             //   exists, used for setup/enumeration traffic.
                             //   Allowed values: 8, 16, 32, or 64. That's it.
                             //   64 is the max for USB Full Speed and means
                             //   fewer round-trips when transferring descriptors.
                             //   The RP2040 supports 64, so we use it.

    // --- Your choice: device identity ---

    0x2E, 0x23,          // idVendor: 0x232E
                         //   USB Vendor IDs are assigned by the USB-IF and
                         //   cost money. This is Raspberry Pi Trading's VID.
                         //   For a personal project, the host doesn't check
                         //   — any value works because HID uses generic
                         //   OS drivers, not vendor-specific ones.

    0xDA, 0x28,          // idProduct: 0x28DA
                         //   Product ID — arbitrary, your choice.
                         //   Together with idVendor, this is the "fingerprint"
                         //   the host uses to recognize your device.
                         //   I made this number up. It has no meaning.

    0x00, 0x01,          // bcdDevice: 1.00
                         //   Your firmware version number. BCD, little-endian.
                         //   Bump this when you release updates so the host
                         //   can distinguish firmware revisions.

    // --- Your choice: which strings to show (or 0 for "none") ---

    0x01,                // iManufacturer: string index 1
                         //   Points to "Raspberry Pi" in the string table below.
                         //   Setting this to 0 would mean "no manufacturer string."

    0x02,                // iProduct: string index 2
                         //   Points to "Atari Adapter" in the string table.

    0x03,                // iSerialNumber: string index 3
                         //   Points to "A2600-JOY-001" in the string table.
                         //   If you build multiple adapters, vary this per unit
                         //   so the host can tell them apart.

    0x01                 // bNumConfigurations: 1
                         //   How many configurations this device offers.
                         //   Almost all simple devices have 1. Multiple configs
                         //   are for devices that can switch power modes, etc.
};

// =====================================================================
// HID REPORT DESCRIPTOR
// =====================================================================
// This is a compact bytecode language that describes the structure
// of your HID reports — what fields exist, how big they are, and
// what each value means. The host parses this to understand the
// 1-byte reports your code sends over USB.
//
// The format uses ITEMS. Each item is a prefix byte followed by
// 0-4 data bytes. The prefix encodes:
//   bits [1:0] = data size (0/1/2/4 bytes)
//   bits [3:2] = item type (Main, Global, or Local)
//   bits [7:4] = tag (what the item does)
//
// Global items set state that sticks around for following items.
// Local items set state that applies only to the next Main item.
// Main items (Input, Collection, End Collection) do the actual work.
//
// The usage page/usage values (like 0x01 for Generic Desktop, 0x04
// for Joystick, 0x39 for Hat Switch) are assigned by the USB HID
// Usage Tables document — a separate spec from core USB. These are
// fixed numbers, not things you invent.
// =====================================================================

static uint8_t const hid_report_desc[] = {
    // --- Fixed by spec: declare the device type ---

    0x05, 0x01,        // [Global] Usage Page: Generic Desktop (0x01)
                       //   Usage pages are namespaces. 0x01 = Generic Desktop,
                       //   which covers mice, keyboards, joysticks, gamepads.
                       //   This number is assigned by the HID Usage Tables spec.

    0x09, 0x05,        // [Local] Usage: Gamepad (0x05)
                       //   Within the Generic Desktop page, 0x04 = Joystick.
                       //   Also assigned by the spec — you can't pick a random number.
                       //   Other options in this page: Mouse=0x02, Keyboard=0x06,
                       //   Gamepad=0x05. Joystick and Gamepad are nearly interchangeable
                       //   for most host software, but Joystick is more traditional
                       //   for a device with a hat switch.

    0xA1, 0x01,        // [Main] Collection: Application (0x01)
                       //   Opens a group of controls that belong to one logical
                       //   device. The host treats everything inside this block as
                       //   belonging to a single joystick. 0x01 = Application type.
                       //   (0x00 = Physical, 0x02 = Logical — neither is what we want.)
                       //   Everything below is inside this collection until 0xC0 at
                       //   the bottom.

    // --- Hat switch (D-pad) ---
    // The four directional switches on the Atari stick become one
    // hat switch — a single field reporting a compass direction.
    // This is semantically correct: the host sees a D-pad, not
    // four random buttons.

    0x05, 0x01,        // [Global] Usage Page: Generic Desktop (0x01)
                       //   Re-stated because the hat switch usage (0x39) lives
                       //   under the Generic Desktop page, not the Button page.
                       //   Global items persist, but being explicit avoids confusion.

    0x09, 0x39,        // [Local] Usage: Hat Switch (0x39)
                       //   Fixed by spec: within Generic Desktop, 0x39 = Hat Switch.
                       //   This is the standard HID way to represent a D-pad.
                       //   The host knows to interpret this as a directional input,
                       //   not a button. Emulators and games look for this specifically.

    0x15, 0x00,        // [Global] Logical Minimum: 0
                       //   Smallest value we'll report for the hat. 0 = Up.

    0x25, 0x07,        // [Global] Logical Maximum: 7
                       //   Largest value we'll report. 0-7 = eight compass directions.
                       //   8 = centered (no direction pressed). We include 8 so the
                       //   host treats centered as a valid, intentional value rather
                       //   than an out-of-range error.

    0x35, 0x00,        // [Global] Physical Minimum: 0
                       //   Maps logical values to physical units. 0 = 0 degrees.

    0x46, 0x3B, 0x01,  // [Global] Physical Maximum: 315
                       //   315 degrees = 7 positions × 45° each.
                       //   Position 0 = 0°, position 1 = 45°, ..., position 7 = 315°.
                       //   Position 8 (centered) falls outside this range — that's fine.
                       //   This is the standard hat switch physical mapping that
                       //   every host expects. Don't change it without reason.

    0x65, 0x14,        // [Global] Unit: English Rotation, Degrees (0x14)
                       //   Fixed by spec: 0x14 = rotational degrees in the English
                       //   unit system. Every standard hat switch descriptor uses this.
                       //   It gives meaning to the Physical Min/Max above — they're
                       //   in degrees, not radians or arbitrary units.

    0x55, 0x00,        // [Global] Unit Exponent: 0
                       //   Scaling factor: 10^0 = ×1. No multiplier applied.
                       //   Physical values are already in whole degrees.

    0x75, 0x04,        // [Global] Report Size: 4 bits
                       //   Each hat value occupies 4 bits in the report.
                       //   4 bits can hold 0-15, which covers our 0-8 range.

    0x95, 0x01,        // [Global] Report Count: 1
                       //   One hat switch field. (Two D-pads would be count=2.)

    0x81, 0x02,        // [Main] Input: Data, Variable, Absolute
                       //   Consumes the accumulated state above and declares
                       //   an input field in the report.
                       //
                       //   0x02 breaks down as:
                       //     bit 0 = 0 → Data (real input, not padding)
                       //     bit 1 = 1 → Variable (independent field, not an array)
                       //     bit 2 = 0 → Absolute (report actual position, not a delta)
                       //
                       //   This is the standard Input flag for a hat switch or button.
                       //   The 0x02 value appears in virtually every HID descriptor.

    // --- Fire button ---

    0x05, 0x09,        // [Global] Usage Page: Button (0x09)
                       //   Fixed by spec: 0x09 = Button page. Under this page,
                       //   usage IDs 1, 2, 3, ... map to Button 1, Button 2, etc.

    0x19, 0x01,        // [Local] Usage Minimum: Button 1
                       //   First button in our range. Used together with Usage Maximum
                       //   to define a span of buttons. We only have one.

    0x29, 0x01,        // [Local] Usage Maximum: Button 1
                       //   Last button in our range. Min=1, Max=1 = exactly one button.

    0x15, 0x00,        // [Global] Logical Minimum: 0
                       //   Re-stated because the hat switch set this to 8 above.
                       //   Buttons are boolean: 0 = released.

    0x25, 0x01,        // [Global] Logical Maximum: 1
                       //   1 = pressed. Buttons are on/off — no in-between.

    0x75, 0x01,        // [Global] Report Size: 1 bit
                       //   One bit per button (0 or 1, that's all a button needs).

    0x95, 0x01,        // [Global] Report Count: 1
                       //   One button field = 1 bit.

    0x81, 0x02,        // [Main] Input: Data, Variable, Absolute
                       //   Same 0x02 as the hat switch — Data, Variable, Absolute.
                       //   Adds 1 bit to the report for the fire button.

    // --- Padding ---
    // Hat took 4 bits, fire button took 1 bit = 5 bits used.
    // HID reports should be byte-aligned, so we pad with 3 unused
    // bits to reach 8 bits (1 full byte). Not strictly required by
    // all hosts, but omitting it is asking for trouble.

    0x75, 0x03,        // [Global] Report Size: 3 bits
                       //   8 total - 5 used = 3 bits of padding needed.

    0x95, 0x01,        // [Global] Report Count: 1
                       //   One padding field of 3 bits.

    0x81, 0x03,        // [Main] Input: Constant
                       //   0x03 breaks down as:
                       //     bit 0 = 1 → Constant (filler, not real data)
                       //     bit 1 = 1 → Variable (individual bits)
                       //     bit 2 = 0 → Absolute
                       //
                       //   Tells the host: "these 3 bits exist but carry no
                       //   meaningful information — ignore them."

    0xC0               // [Main] End Collection
                       //   Fixed by spec: 0xC0 always means End Collection.
                       //   Closes the Application Collection opened at the top.
                       //   The host now has a complete report definition:
                       //   1 byte = 4-bit hat + 1-bit button + 3-bit padding.
};

// =====================================================================
// CONFIGURATION DESCRIPTOR
// =====================================================================
// Sent to the host after the device descriptor. Describes the device's
// USB topology: how many interfaces, what class they use, and what
// endpoints they have (endpoints are the pipes data flows through).
//
// This is a nested tree, flattened into one contiguous byte array:
//
//   Configuration header (9 bytes)
//     Interface 0 (9 bytes)
//       HID class descriptor (9 bytes) — points to the report descriptor
//       Endpoint 1 IN (7 bytes) — where we send HID reports
//
// Total: 9 + 9 + 9 + 7 = 34 bytes
// =====================================================================

static uint8_t const desc_configuration[] = {

    // === Configuration Header (9 bytes) ===

    0x09,                               // bLength: 9
                                        //   Fixed by spec: configuration descriptors
                                        //   are always 9 bytes.

    0x02,                               // bDescriptorType: CONFIGURATION
                                        //   Fixed by spec: 0x02 always means config.

    34, 0x00,  // wTotalLength: 34 bytes
                                        //   Total size of THIS descriptor plus ALL
                                        //   sub-descriptors that follow it (interface +
                                        //   HID + endpoint). The host reads exactly
                                        //   this many bytes to get the full tree.

    0x01,                               // bNumInterfaces: 1
                                        //   Your choice: we expose one interface
                                        //   (the HID joystick). More complex devices
                                        //   might have several (HID + audio + storage).

    0x01,                               // bConfigurationValue: 1
                                        //   ID the host uses to activate this config
                                        //   via SET_CONFIGURATION command. With only
                                        //   one config, 1 is the obvious choice.

    0x00,                               // iConfiguration: 0
                                        //   0 = no human-readable name for this config.
                                        //   Could point to a string, but nobody looks at it.

    0x80,                               // bmAttributes: 0x80 = bus-powered
                                        //   bit 7 = 1: MUST be 1 (reserved, historical)
                                        //   bit 6 = 0: Bus-powered (no external power supply)
                                        //   bit 5 = 0: No remote wakeup capability
                                        //   bits 4-0 = 0: Reserved, must be zero
                                        //
                                        //   The only real choice here is bit 6:
                                        //   0 = bus-powered, 1 = self-powered.

    0x32,                               // bMaxPower: 100mA
                                        //   In units of 2mA. 0x32 = 50 × 2mA = 100mA.
                                        //   Your choice, but should be honest.
                                        //   USB guarantees 100mA to any device before
                                        //   configuration, so this is always safe.
                                        //   A bare RP2040 + pull-ups draws far less.
                                        //   Max possible: 0xFA = 250 × 2mA = 500mA.

    // === Interface Descriptor (9 bytes) ===

    0x09,                               // bLength: 9
                                        //   Fixed by spec: interface descriptors
                                        //   are always 9 bytes.

    0x04,                               // bDescriptorType: INTERFACE
                                        //   Fixed by spec: 0x04 always means interface.

    0x00,                               // bInterfaceNumber: 0
                                        //   Your choice: zero-indexed interface number.
                                        //   With one interface, 0 is the only sensible value.

    0x00,                               // bAlternateSetting: 0
                                        //   Your choice: alternate settings let you
                                        //   reconfigure an interface (e.g., switch endpoint
                                        //   sizes). We have one setting, so 0.

    0x01,                               // bNumEndpoints: 1
                                        //   Your choice: we have 1 endpoint (the interrupt
                                        //   IN for HID reports) besides the mandatory EP0.
                                        //   EP0 (control) doesn't count here.

    0x03,                               // bInterfaceClass: HID (0x03)
                                        //   Fixed by spec: 0x03 = HID. This is what tells
                                        //   the host to load its built-in HID driver.
                                        //   No custom drivers needed on any OS.

    0x01,                               // bInterfaceSubClass: 0x01
                                        //   Constrained: 0x00 = no subclass, 0x01 = boot.
                                        //   Boot subclass is designed for keyboards/mice that
                                        //   need BIOS support. A joystick doesn't need boot
                                        //   protocol, so 0x00 would be more precise. But
                                        //   every host accepts either value, and many
                                        //   production gamepads ship with 0x01. It's harmless.

    0x00,                               // bInterfaceProtocol: 0x00
                                        //   Constrained: only meaningful with boot subclass.
                                        //   0x01 = keyboard, 0x02 = mouse, 0x00 = none.
                                        //   For a joystick with no boot protocol, 0x00.

    0x00,                               // iInterface: 0
                                        //   Your choice: 0 = no human-readable name for this
                                        //   interface. Could point to a string descriptor.

    // === HID Class Descriptor (9 bytes) ===
    // Bridges the interface and endpoint. Tells the host where
    // to find the HID report descriptor (the bytecode above).

    0x09,                               // bLength: 9
                                        //   Fixed by spec: HID class descriptors
                                        //   are always 9 bytes.

    0x21,                               // bDescriptorType: HID (0x21)
                                        //   Fixed by spec: 0x21 = HID class-specific
                                        //   descriptor (distinct from the standard types
                                        //   like 0x01=device, 0x02=config, etc.)

    0x10, 0x01,                         // bcdHID: 1.10
                                        //   HID spec version this device conforms to.
                                        //   1.10 is the most widely supported version.
                                        //   Matches our bcdUSB. BCD, little-endian.

    0x00,                               // bCountryCode: 0 (not localized)
                                        //   Non-zero = country-specific keyboard layout.
                                        //   Irrelevant for a joystick. 0 = "not localized."

    0x01,                               // bNumDescriptors: 1
                                        //   How many report descriptors we have.
                                        //   Almost always 1. Complex HID devices
                                        //   could have multiple.

    0x22,                               // bDescriptorType: REPORT (0x22)
                                        //   Fixed by spec: 0x22 = report descriptor.
                                        //   (0x10 = physical descriptor, rarely used.)

    sizeof(hid_report_desc), 0x00,      // wDescriptorLength
                                        //   Size of the HID report descriptor above.
                                        //   The host sends GET_DESCRIPTOR(REPORT) and
                                        //   reads exactly this many bytes.

    // === Endpoint Descriptor (7 bytes) ===
    // Describes the pipe that carries HID reports from device to host.

    0x07,                               // bLength: 7
                                        //   Fixed by spec: endpoint descriptors
                                        //   are always 7 bytes.

    0x05,                               // bDescriptorType: ENDPOINT
                                        //   Fixed by spec: 0x05 always means endpoint.

    0x81,                               // bEndpointAddress: 0x81
                                        //   bit 7 = 1 → IN direction (device → host)
                                        //   bits 3:0 = 1 → Endpoint number 1
                                        //
                                        //   Direction is your choice (IN for reports going
                                        //   TO the host — that's what we want). Endpoint
                                        //   number is your choice but 1 is universal convention.

    0x03,                               // bmAttributes: INTERRUPT
                                        //   Constrained: HID requires interrupt transfers.
                                        //   bits 1:0 = 3 → Interrupt transfer type.
                                        //   Interrupt endpoints guarantee bounded latency —
                                        //   the host polls at fixed intervals. This is
                                        //   mandatory for HID per the spec.
                                        //   (Bulk=0x02 and Isochronous=0x01 are for other
                                        //    device classes and don't work for HID.)

    0x08, 0x00,                         // wMaxPacketSize: 8 bytes
                                        //   Your choice, within limits. Our report is 1 byte,
                                        //   but 8 is the practical minimum for interrupt
                                        //   endpoints. If we later added analog axes (making
                                        //   reports bigger), we'd increase this accordingly.

    0x01                                // bInterval: 1ms
                                        //   Your choice: how often the host polls this
                                        //   endpoint. 1ms = fastest possible for Full Speed USB.
                                        //   Standard for gaming input. 10ms would work but feel
                                        //   sluggish. Range: 1-255ms.
};

// =====================================================================
// STRING DESCRIPTORS
// =====================================================================
// Human-readable names that show up in System Information, lsusb,
// device managers, etc. The device descriptor references these by
// index (iManufacturer=1, iProduct=2, iSerialNumber=3).
//
// USB requires strings to be UTF-16LE encoded with a 2-byte header:
//   byte 0 = total byte length (including header)
//   byte 1 = 0x03 (descriptor type = STRING)
//   bytes 2+ = UTF-16LE characters (2 bytes per ASCII char)
//
// Index 0 is special: it returns supported language codes, not text.
// All other indices return UTF-16LE text strings.
//
// The actual string contents are entirely your choice.
// =====================================================================

#define STRING_DESCRIPTOR_COUNT 4

static char const *string_table[STRING_DESCRIPTOR_COUNT] = {
    "\x09\x04",            // [0] Language ID (special — not a text string)
                           //   Raw bytes for 0x0409 = English (US).
                           //   Handled separately in the callback below.

    "Raspberry Pi",        // [1] Manufacturer — your choice, shows in System Info
    "Atari Adapter",       // [2] Product name — your choice, shows in System Info
    "A2600-JOY-001"        // [3] Serial number — your choice, vary per unit if making multiples
};

// =====================================================================
// TINYUSB CALLBACKS
// =====================================================================
// TinyUSB is a generic USB stack — it handles the low-level protocol
// but doesn't know your device's specific descriptors. Instead, it
// defines callback functions that you implement. During enumeration,
// TinyUSB calls these to get pointers to your descriptor data.
//
// This separation lets TinyUSB support devices with static descriptors
// (like ours), dynamically generated descriptors, or multi-config
// devices — all with the same API.
// =====================================================================

// Host asks: "Give me your device descriptor."
uint8_t const *tud_descriptor_device_cb(void) {
    return desc_device;
}

// Host asks: "Give me your configuration descriptor tree."
// index selects between multiple configs; we only have one.
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

// Host asks: "Give me your HID report descriptor."
// This is how the host learns our report layout (hat + button + padding).
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;  // Unused — we only have one HID device
    return hid_report_desc;
}

// Host asks: "Give me string descriptor N."
// We convert ASCII strings to the UTF-16LE format USB requires.
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;  // We only support English, so we ignore the requested language

    if (index >= STRING_DESCRIPTOR_COUNT) {
        return NULL;  // We don't have a string for this index
    }

    static uint16_t str_buf[64];  // Reused across calls — TinyUSB copies the data

    if (index == 0) {
        // Language ID list — special case, not a text string.
        // Tells the host which languages this device supports.
        // We support English (US) only.
        //
        // Format: byte 0=4 (total length), byte 1=0x03 (STRING type),
        //         bytes 2-3=0x0409 (English US, little-endian)
        str_buf[0] = 0x0304;  // Packed: low byte=4 (length), high byte=0x03 (type)
        str_buf[1] = 0x0409;  // English US language code
        return str_buf;
    }

    // Regular string — convert ASCII to UTF-16LE
    char const *str = string_table[index];
    size_t char_count = strlen(str);

    // Total byte length = 2-byte header + 2 bytes per character
    size_t total_bytes = 2 + (char_count * 2);

    // Header: low byte = total length, high byte = 0x03 (STRING type)
    // Example: "Atari Adapter" = 13 chars → 2 + 26 = 28 bytes
    //   str_buf[0] = 28 | (0x03 << 8) = 0x031C
    //   In memory: byte 0 = 0x1C (28), byte 1 = 0x03 (STRING)
    str_buf[0] = (total_bytes & 0xFF) | (0x03 << 8);

    // Each ASCII character becomes a 16-bit UTF-16LE code point.
    // For ASCII (0-127), this is just the char value with a zero high byte:
    //   'A' (0x41) → 0x0041
    for (size_t i = 0; i < char_count && i < 63; i++) {
        str_buf[i + 1] = (uint16_t)str[i];
    }

    return str_buf;
}