# zmk-rng-typer

A ZMK custom behavior that uses the **nRF52 hardware True RNG** (TRNG) to type random output directly into any focused field.

## Features

| Mode | param1 | Output |
|------|--------|--------|
| Dice roll | `0` | `d20: 14` — dice notation + result |
| Random integer | `1` | `3849204817` — full raw 32-bit value (0–4294967295) |
| Random string | `2` | `aK3mZx9RpL0wQv7T` — 16 chars |

All randomness comes from the **nRF52840 TRNG** (`rng` Zephyr entropy driver), not a PRNG. Each key press is a fresh hardware sample.

---

## File Layout

```
zmk-rng-behavior/
├── behaviors/rng_typer/
│   ├── CMakeLists.txt
│   ├── Kconfig
│   └── src/rng_typer.c        ← main behavior implementation
├── dts/bindings/behaviors/
│   └── zmk,behavior-rng-typer.yaml
└── example.keymap
```

---

## Installation

### 1. Add as a ZMK module

In your `west.yml` (or `config/west.yml` for a ZMK config repo):

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: mikel247                       # ← change this
      url-base: https://github.com/yourname

  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml

    - name: zmk-rng-behavior                # ← add this
      remote: mikel247
      revision: main

  self:
    path: config
```

### 2. Enable in `Kconfig.defconfig` / `<board>.conf`

```conf
CONFIG_ZMK_BEHAVIOR_RNG_TYPER=y
CONFIG_ENTROPY_GENERATOR=y          # usually already on for nice!nano
```

### 3. Add the behavior node (in your `.keymap` or a shared `behaviors.dtsi`)

```dts
/ {
    behaviors {
        rng_typer: rng_typer {
            compatible = "zmk,behavior-rng-typer";
            #binding-cells = <2>;
        };
    };
};
```

### 4. Bind keys

```dts
/* Mode 0 – Dice rolls */
&rng_typer 0 4     /* d4  */
&rng_typer 0 6     /* d6  */
&rng_typer 0 20    /* d20 */
&rng_typer 0 100   /* d100 */

/* Mode 1 – Raw integer */
&rng_typer 1 100   /* random 0-99 */
&rng_typer 1 256   /* random 0-255 */

/* Mode 2 – Random 16-char string */
&rng_typer 2 0     /* alphanumeric: a-z A-Z 0-9 */
&rng_typer 2 1     /* hex: 0-9 a-f */
&rng_typer 2 2     /* lowercase + digits */
&rng_typer 2 3     /* alphanumeric + symbols */
```

---

## Dice output format

```
d20: 14
d6: 3
d100: 87
```

The `d` prefix and colon are typed as keystrokes so the output pastes naturally into any text field, chat, or terminal.

---

## Notes

- The nRF52840 TRNG generates 1 byte at a time from thermal noise; `rng_range()` uses rejection sampling to eliminate modulo bias.
- Key events are raised synchronously in the binding-pressed handler. Very long strings will block briefly — this is expected ZMK behavior identical to macros.
- The behavior has no persistent state; each keypress is fully independent.
- Tested against ZMK `main` branch (post-2024 Zephyr 3.x era). If your ZMK is older, the `raise_zmk_keycode_state_changed_from_encoded` signature may differ slightly.

---

## License

MIT

