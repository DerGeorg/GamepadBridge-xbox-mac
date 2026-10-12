# GamepadBridge trailer — storyboard

1920×1080, 30 fps, 40 s. English on screen, like the product.
Brand (from the website): navy `#1b1f24`, near-black `#0d0f11`, green
`#2bbd84`, yellow `#fbb040`, grey `#a8b0b8`. Text font Inter, terminal
JetBrains Mono (`@remotion/google-fonts`). No Microsoft or Apple logos:
the controller is a generic drawn gamepad, the Mac a drawn menu bar.

| # | Scene | Seconds | What happens |
|---|---|---|---|
| 1 | Hook | 0–4 | A drawn controller and dongle. "Your Mac ignores your Xbox controller." |
| 2 | Problem | 4–8 | The dongle slides into a Mac. "No driver. No device. Nothing." |
| 3 | Reveal | 8–12 | The app icon springs in, "GamepadBridge", badge "Xbox One & Xbox 360 · free · open source". |
| 4 | Install | 12–18 | The icon is dragged to Applications, then both permissions switch on. |
| 5 | Pair | 18–27 | The menu bar item appears, the menu opens ("No controller"), "Pair a Controller" is clicked, the controller's button blinks, then "Controller 1 · Battery: full" — "Connected. That's it." |
| 6 | Xbox 360 | 27–32 | A 360 controller's ring spins, lights quarter 1, "Xbox 360 controller 1" joins the menu, the pad rumbles. |
| 7 | Features | 32–36 | Four cards: several at once, rumble, the Xbox button, signed and updating itself. |
| 8 | Outro | 36–40 | Icon, "GamepadBridge", gamepadbridge.dergeorg.at. |

The exact times of every cue are in `src/timeline.json`.

## Sound (generated, `audio/make_audio.py`, standard library only)

- Music: 112 BPM in A minor. A quiet pad with a heartbeat pulse while the
  Mac ignores the controller, a riser into the reveal, a groove (kick, clap,
  hats, bass) under the demo, a light arpeggio while controllers connect,
  a last chord on the logo, a fade over the final 1.5 s.
- Effects on the cues in `src/timeline.json`: a whoosh on every cut, a thud
  and click as the dongle plugs in, the drop into Applications, clicks and a
  tone for each permission, clicks in the menu, a chime when each controller
  connects, a low growl for the rumble, a pluck per feature card.
- The music ducks under the effects that matter.
- Output `public/trailer-audio.wav`, played with `<Audio>` from `@remotion/media`.

## Deliverables

- `out/trailer.mp4` (H.264 + AAC, small enough for the website).
- Website: `<video controls preload="metadata" poster>` in the hero or
  under "How it works".
- README: a poster image linking to the video on the website (GitHub does
  not play an mp4 from the repository; GitLab would).

## State

Built and rendered (`out/trailer.mp4`, 6.6 MB). The music ducks under the
effects that matter (connecting chimes, the drop, the toggles, the rumble).
