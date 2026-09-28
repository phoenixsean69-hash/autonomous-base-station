# ABS Connect Mobile

ABS Connect is the mobile calling application used by the Autonomous Base Station project.

The interface is deliberately written for an ordinary phone user. Technical measurements remain available internally to the simulator and engineering systems, while the app presents simple descriptions such as **Strong signal**, **Good call quality**, **Low call delay**, and **No interruptions**.

## Demo numbers

- Phone 1: `0712 000 001`
- Phone 2: `0712 000 002`

Each device or emulator must choose a different number.

## Start the call server

From the project root:

```powershell
cd network_simulator
.\run.ps1
```

Keep that terminal running.

## Start the mobile app

In another terminal:

```powershell
cd mobile
npm install
npx expo start -c
```

On two devices/emulators:

1. Device 1 chooses `0712 000 001`.
2. Device 2 chooses `0712 000 002`.
3. Wait until both show **Mobile network — Connected**.
4. Dial the other number.
5. The other phone receives a real in-app incoming-call event.
6. Answer the call.
7. Live network measurements update the user-friendly call-quality labels.
8. Ending the call adds it to **Recents**.

## Server discovery

During Expo development the app tries to use the same computer hosting the Expo development server and port `8000`.

If that does not work, create `mobile/.env` from `.env.example` and set:

```text
EXPO_PUBLIC_NETWORK_SIMULATOR_URL=ws://YOUR_LAPTOP_IP:8000
```

Then restart Expo with `npx expo start -c`.
