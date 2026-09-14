
# Logitech Unifying Protocol Implementation

![Arduino nano and nRF24L01+ on breadboard](https://raw.githubusercontent.com/decrazyo/unifying/main/doc/breadboard.jpg)

This project is an attempt to re-implement the proprietary Logitech Unifying protocol as a free and open C library.
The library is intended to be Arduino compatible while remaining compiler and hardware agnostic.
The goal of this project is to enable people to create custom keyboards and mice that are compatible with Logitech Unifying receivers.

## Example
The provided Arduino example is dependent on the RF24 library.
https://github.com/nRF24/RF24

Pro Micro nRF52840 (nRF Connect SDK + ESB + USB CDC) example:
[`examples/promicro_nrf52840/`](examples/promicro_nrf52840/)

## GitHub Codespaces

Open this repo in [Codespaces](https://github.com/codespaces). The Pro Micro build requires nRF Connect SDK **v3.4.0** with the `promicro_nrf52840` board definition. The setup script selects the existing `/root/ncs/v3.4.0` installation and refuses to fall back to the incompatible `/workdir` NCS 2.9 environment.

```bash
./scripts/build-promicro.sh
```

Firmware: `examples/promicro_nrf52840/build/promicro_nrf52840/zephyr/zephyr.uf2` (or `.hex`).

**Flash check:** after UF2 copy, a USB serial port must appear and show `=== ProMicro Unifying CLI ===`. Unifying receivers do **not** BLE-scan for this device — use serial `pair` after the receiver pairing button. See [examples/promicro_nrf52840/README.md](examples/promicro_nrf52840/README.md).

## TODO
- [ ] Add proper HID++ response payloads
- [ ] Add more examples
- [ ] General code cleanup

## Done
- [x] Timing-critical packet transmission
- [x] Pairing with a receiver
- [x] HID++ error response payloads
- [x] Encrypted keystroke payloads
- [x] Add documentation
- [x] Add mouse payloads
- [x] Add multimedia payloads
- [x] Add wake up payloads

## See also
[Hacking Logitech Unifying DC612 talk](https://www.youtube.com/watch?v=10lE96BBOF8)  
[nRF24 pseudo-promiscuous mode](http://travisgoodspeed.blogspot.com/2011/02/promiscuity-is-nrf24l01s-duty.html)  
[KeySweeper](https://github.com/samyk/keysweeper)  
[MouseJack](https://github.com/BastilleResearch/mousejack)  
[KeyJack](https://github.com/BastilleResearch/keyjack)  
[KeySniffer](https://github.com/BastilleResearch/keysniffer)  
[Of Mice And Keyboards](https://www.icaria.de/posts/2016/11/of-mice-and-keyboards/)  
[Logitech HID++ Specification](https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28)  
[Official Logitech Firmware](https://github.com/Logitech/fw_updates)  
