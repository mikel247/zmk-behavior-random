# zmk-rng-behavior

A ZMK custom behavior that uses the **nRF52 hardware True RNG** (TRNG) to type random output directly into any focused field.

## Features

| Mode | param1 | Output |
|------|--------|--------|
| Dice roll | `0` | `d20: 14` — dice notation + result |
| Random integer | `1` | `3849204817` — full raw 32-bit value (0–4294967295) |
| Random 16-char string | `2` | `aK3!mZx9Rp@L0w$T` — alphanum + symbols |

Add `0x80` to any `param1` to append an Enter keypress after the output:

| Binding | Output |
|---------|--------|
| `&rng_typer 0x80 20` | `d20: 14 <RET>` |
| `&rng_typer 0x81 0`  | `3849204817 <RET>` |
| `&rng_typer 0x82 3`  | `aK3!mZx9... <RET>` |

All randomness comes from the **nRF52840 TRNG** (`rng` Zephyr entropy driver), not a PRNG. Each key press is a fresh hardware sample.

---

## File Layout

```
zmk-rng-behavior/
├── zephyr/
│   └── module.yml                           ← registers module with west
├── dts/
│   └── bindings/
│       └── behaviors/
│           └── zmk,behavior-rng-typer.yaml  ← devicetree binding
├── behaviors/rng_typer/
│   ├── CMakeLists.txt
│   ├── Kconfig
│   └── src/rng_typer.c                      ← main behavior implementation
├── CMakeLists.txt                           ← root CMake, sourced by module.yml
├── Kconfig                                  ← root Kconfig, sources rng_typer/Kconfig
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
    - name: yourname                        # ← change this
      url-base: https://github.com/yourname

  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml

    - name: zmk-rng-behavior                # ← add this
      remote: yourname
      revision: main

  self:
    path: config
```

### 2. Enable in your board's `.conf`

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
/* Dice rolls */
&rng_typer 0 4     /* d4   */
&rng_typer 0 6     /* d6   */
&rng_typer 0 20    /* d20  */
&rng_typer 0 100   /* d100 */

/* Full 32-bit random integer */
&rng_typer 1 0

/* Random 16-char string */
&rng_typer 2 0     /* alphanumeric a-z A-Z 0-9 */
&rng_typer 2 1     /* hex 0-9 a-f */
&rng_typer 2 2     /* lowercase a-z 0-9 */
&rng_typer 2 3     /* alphanumeric + symbols */

/* Same bindings with Enter appended */
&rng_typer 0x80 20  /* d20 + RET  */
&rng_typer 0x81 0   /* int + RET  */
&rng_typer 0x82 3   /* str + RET  */
```

---

## Tuning key timing

The default inter-key delay is **30ms**, matching ZMK's macro default. Override in your `.conf` if needed:

```conf
# Faster — fine for USB or a reliable BLE host
CONFIG_ZMK_RNG_TYPER_WAIT_MS=15

# Safer — for Windows or congested 2.4 GHz environments
CONFIG_ZMK_RNG_TYPER_WAIT_MS=50
```

A 16-char symbol string at 30ms takes roughly **~2 seconds** to type (each shifted character needs 4 key events). Drop to 15ms if that feels slow.

---

## Security notes

- The nRF52840 TRNG uses thermal noise — genuine hardware entropy, not a software PRNG.
- **BLE HID is unencrypted in many host configurations.** Anyone within radio range with a BLE sniffer can read keystrokes. This applies to all BLE keyboards.
- Characters appear on screen as they are typed — avoid use in sight of others.
- Dropped BLE packets can silently corrupt a generated password — consider typing into a visible field first to verify.
- For password generation, USB is significantly safer than BLE.

---

## License

MIT
