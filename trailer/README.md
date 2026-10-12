# GamepadBridge trailer

A 40-second trailer for the website and the README, made with
[Remotion](https://www.remotion.dev). The scenes and the storyboard are in
`src/` and `STORYBOARD.md`.

Picture and sound share one timeline, `src/timeline.json`: scene lengths and
the moments the sound effects hit (the dongle plugging in, the controller
connecting, the rumble). Change a time there, regenerate the sound, render.

```sh
nvm use 24                       # Node 24
npm install
python3 audio/make_audio.py      # public/trailer-audio.wav, standard library only
npx remotion studio              # preview
npx remotion render Trailer out/trailer.mp4 --codec=h264 --crf=22
```

Nothing in it is a picture of anyone's product: the controllers, the dongle
and the Mac are drawn, and the menu is the app's own menu as of 1.2.0.

Remotion is free for individuals and teams of up to three; see
[remotion.pro/license](https://www.remotion.pro/license).
