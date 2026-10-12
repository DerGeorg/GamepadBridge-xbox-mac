import React from "react";
import { AbsoluteFill, Easing, interpolate, useCurrentFrame } from "remotion";
import { Backdrop, Cursor, Gamepad, Headline, Menu, MenuBar, MenuLine } from "../parts";
import { clamp, COLORS, CUES, sec } from "../theme";
import { Fade } from "./Fade";

const move = { ...clamp, easing: Easing.bezier(0.45, 0, 0.2, 1) };

// Where the menu bar glyph and the "Pair" row sit on screen.
const GLYPH = { x: 1622, y: 12 };
const PAIR_ROW = { x: 1330, y: 168 };

export const Pair: React.FC = () => {
  const frame = useCurrentFrame();
  const c = CUES.Pair;
  const iconIn = sec(c.iconIn);
  const open = sec(c.menuOpen);
  const click = sec(c.pairClick);
  const connected = sec(c.connected);

  const isConnected = frame >= connected;

  const lines: MenuLine[] = [
    isConnected
      ? { kind: "status", text: "Controller 1 · Battery: full", key: "c1" }
      : { kind: "status", text: "No controller", key: "none" },
    { kind: "separator" },
    {
      kind: "action",
      text: "Pair a Controller",
      highlighted: frame >= click - 14 && frame < click + 10,
    },
    { kind: "separator" },
    { kind: "status", text: "GamepadBridge 1.2.0" },
    { kind: "action", text: "Check for Updates…" },
    { kind: "action", text: "Open at Login", checked: true },
    { kind: "separator" },
    { kind: "action", text: "Quit GamepadBridge" },
  ];

  // The controller's centre button blinks while it searches, then stays lit.
  const searching = frame >= click + 6 && frame < connected;
  const glow = isConnected
    ? 1
    : searching
      ? (Math.sin((frame - click) / 3) + 1) / 2
      : 0;

  return (
    <Fade>
      <Backdrop />
      <MenuBar iconIn={interpolate(frame, [iconIn, iconIn + 8], [0, 1], clamp)} filled={isConnected} />
      <Menu
        lines={lines}
        right={265}
        open={interpolate(frame, [open, open + 6], [0, 1], clamp)}
        appear={{ c1: interpolate(frame, [connected, connected + 8], [0.4, 1], clamp) }}
      />

      <AbsoluteFill style={{ paddingLeft: 130, paddingTop: 210, alignItems: "flex-start" }}>
        <div style={{ position: "absolute", top: 210, left: 130, opacity: interpolate(frame, [connected - 6, connected], [1, 0], clamp) }}>
          <Headline size={84} style={{ textAlign: "left" }}>
            <span style={{ color: COLORS.green }}>3</span>&nbsp; Pair your controller
          </Headline>
        </div>
        <div style={{ position: "absolute", top: 210, left: 130 }}>
          <Headline at={connected} size={84} color={COLORS.green} style={{ textAlign: "left" }}>
            Connected. That&apos;s it.
          </Headline>
        </div>
      </AbsoluteFill>

      <div style={{ position: "absolute", left: 150, top: 470 }}>
        <Gamepad
          width={760}
          glow={glow}
          pressed={frame >= click + 6 && frame < click + 14 ? 1 : 0}
          style={{ scale: `${1 + (isConnected ? interpolate(frame, [connected, connected + 6, connected + 14], [0, 0.04, 0], clamp) : 0)}` }}
        />
      </div>

      <Cursor
        x={interpolate(frame, [0, open - 2, click - 16, click, click + 30], [1200, GLYPH.x + 6, PAIR_ROW.x, PAIR_ROW.x, 1300], move)}
        y={interpolate(frame, [0, open - 2, click - 16, click, click + 30], [700, GLYPH.y + 8, PAIR_ROW.y, PAIR_ROW.y, 760], move)}
        press={Math.abs(frame - open) < 3 || Math.abs(frame - click) < 3 ? 1 : 0}
      />
    </Fade>
  );
};
