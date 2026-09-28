# ABS Connect Mobile

A fresh Expo / React Native mobile application for the Autonomous Base Station project.

This app is intentionally created from scratch inside the main base-station repository. It uses the visual language of the MediReach mobile application as a design reference:

- Mulish typography
- white canvas
- charcoal controls
- soft grey cards
- thin borders
- compact labels
- rounded cards and controls
- Lucide icons

## Current UI-only flow

1. Select Subscriber A or Subscriber B
2. Open dialer
3. Dial the peer subscriber
4. Preview outgoing call
5. Preview incoming call
6. Answer and view active-call metrics
7. View recents
8. View network status

Demo subscribers:

- 0712 000 001
- 0712 000 002

## Run

```powershell
npm install
npx expo start
```

The FastAPI/WebSocket call and network simulator is deliberately not included yet.
