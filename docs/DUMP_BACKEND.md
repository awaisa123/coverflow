# Dump Game / App backend

Cover Flow delegates actual package dumping to [EchoStretch/ps5-app-dumper](https://github.com/EchoStretch/ps5-app-dumper). Cover Flow does not implement PS5 package extraction itself.

## Required on the console

- A jailbroken PS5 firmware supported by the version of PS5 App Dumper you install. Check the upstream README before using it.
- The PS5 payload loader (`elfldr.elf`) running so Cover Flow can send a payload to port 9021.
- A compatible `ps5-app-dumper.elf` payload available at one of the locations checked by Cover Flow:
  - the Cover Flow app root;
  - `/data/homebrew/PPSA99123/ps5-app-dumper.elf`;
  - `/mnt/usb0/homebrew/ps5-app-dumper.elf` or `/mnt/usb1/homebrew/ps5-app-dumper.elf`;
  - `/mnt/usb0/ps5-app-dumper/ps5-app-dumper.elf` or `/mnt/usb1/ps5-app-dumper/ps5-app-dumper.elf`.
- A writable destination selected by the dumper. USB dumping requires a compatible USB drive connected to the console.

Use the upstream project's official instructions to obtain and deploy the payload. This repository does not bundle a compiled dumper binary.

## How the integration works

1. Cover Flow checks the local dumper web API on ports 8081–8090.
2. If the service is not running, Cover Flow tries to send the installed payload through the local payload loader and checks again.
3. Cover Flow asks the dumper for writable destinations and submits the selected title ID to `POST /api/queue/start`.
4. The dumper's own queue launches/mounts the selected title and performs the dump. Cover Flow reports that the request was queued, not that the dump has completed.

**Important UI limitation:** starting a game can close Cover Flow. The dumper queue is designed to continue on the console; monitor the real progress, errors, and completion in the PS5 App Dumper web interface/log. The Cover Flow message “DUMP QUEUED” is only an acknowledgement that the dumper accepted the queue request.

## Safety and validation

- The integration does not implement decryption, bypass access controls, or modify the dumper's extraction logic.
- Do not unplug the destination drive while a dump is running.
- Never treat a queued request as a completed or verified backup.
- Build with the PS5 SDK and test on a compatible console before publishing a release. Cover Flow has not been console-tested as part of this integration.
