import React from "react";
import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { Backdrop, Badge, Gamepad, Headline, Menu, MenuBar, MenuLine } from "../parts";
import { clamp, COLORS, CUES, sec } from "../theme";
import { Fade } from "./Fade";

export const Xbox360: React.FC = () => {
  const frame = useCurrentFrame();
  const c = CUES.Xbox360;
  const ring = sec(c.ring);
  const rumble = sec(c.rumble);
  const rumbleEnd = sec(c.rumbleEnd);

  const lines: MenuLine[] = [
    { kind: "status", text: "Controller 1 · Battery: full" },
    { kind: "status", text: "Xbox 360 controller 1", key: "x1" },
    { kind: "separator" },
    { kind: "action", text: "Pair an Xbox One Controller" },
    { kind: "action", text: "Pair an Xbox 360 Controller…" },
    { kind: "separator" },
    { kind: "status", text: "GamepadBridge 1.2.0" },
    { kind: "action", text: "Check for Updates…" },
    { kind: "action", text: "Open at Login", checked: true },
    { kind: "separator" },
    { kind: "action", text: "Quit GamepadBridge" },
  ];

  const shaking = frame >= rumble && frame < rumbleEnd;

  return (
    <Fade>
      <Backdrop glow={COLORS.green} />
      <MenuBar iconIn={1} filled />
      <Menu
        lines={lines}
        right={265}
        open={1}
        appear={{ x1: interpolate(frame, [ring, ring + 8], [0, 1], clamp) }}
      />

      <AbsoluteFill style={{ paddingLeft: 130, paddingTop: 210, alignItems: "flex-start" }}>
        <Headline size={66} style={{ textAlign: "left" }}>Xbox 360 controllers too.</Headline>
        <Headline at={10} size={44} weight={400} color={COLORS.grey} style={{ textAlign: "left", marginTop: 18 }}>
          Up to four, on their own receiver —
          <br />
          right next to your Xbox One pads.
        </Headline>
      </AbsoluteFill>

      <div
        style={{
          position: "absolute",
          left: 170,
          top: 520,
          translate: shaking ? `${Math.sin(frame * 2.7) * 9}px ${Math.cos(frame * 3.3) * 5}px` : "0px 0px",
        }}
      >
        <Gamepad
          width={700}
          variant="x360"
          ringSpin={frame < ring ? frame / 4 : -1}
          ring={frame >= ring ? 0 : -1}
        />
      </div>

      <div
        style={{
          position: "absolute",
          left: 960,
          top: 760,
          opacity: interpolate(frame, [rumble, rumble + 8], [0, 1], clamp),
        }}
      >
        <Badge>Rumble ✓</Badge>
      </div>
    </Fade>
  );
};
