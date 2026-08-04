# sim/harness/

Peripheral models and the simulation core, compiled to objects and linked into
every test binary. Each header is the observation API the tests use.

## Files

| File | What | When to read |
| ---- | ---- | ------------ |
| `nava_sim.h` / `nava_sim.c` | Core: ELF load, EEPROM seed, run loop | Changing how a test starts or steps the simulation |
| `event_log.h` / `event_log.c` | Cycle-stamped observable events, and `event_log_find_step_onset()` | Timing an observable; the trigger word is written twice per step, so adjacent events are not a step period |
| `spi_bus.h` / `spi_bus.c` | 74HC165 inputs, 74HC595 LED/trigger outputs, MCP4822 DAC | Observing triggers or velocity; adding a device on the chain |
| `gpio.h` / `gpio.c` | MUX attribution, encoder injection, DIN and TRIG-OUT logging | Driving the encoder; attributing a trigger to a voice |
| `lcd.h` / `lcd.c` | HD44780 4-bit write-only model with a text mirror | Reading the screen; CGRAM aliases row 0, so raw row 1 is the trustworthy one |
| `midi.h` / `midi.c` | USART1 MIDI TX capture and RX injection | Asserting on transmitted notes; injecting clock or notes |
| `frontpanel.h` / `frontpanel.c` | Named button press/release, step buttons, decoded LED accessors | Driving the panel; adding a button or LED name |
