import React from "react";
import { AbsoluteFill, Easing, interpolate, useCurrentFrame } from "remotion";
import { Backdrop, Dongle, Headline } from "../parts";
import { clamp, COLORS, CUES, sec } from "../theme";
import { Fade } from "./Fade";

export const Problem: React.FC = () => {
  const frame = useCurrentFrame();
  const plug = sec(CUES.Problem.plug);

  return (
    <Fade>
      <Backdrop glow={COLORS.grey} />
      {/* A Mac's side, with a USB port the dongle slides into. */}
      <div
        style={{
          position: "absolute",
          left: 1180,
          top: 420,
          width: 560,
          height: 220,
          borderRadius: 36,
          background: "linear-gradient(180deg, #d5dade, #9aa3ac)",
          boxShadow: "0 40px 80px rgba(0,0,0,0.55)",
        }}
      >
        {/* The port the dongle goes into, and a power light. */}
        <div style={{ position: "absolute", left: 40, top: 100, width: 36, height: 18, background: "#0d0f11", borderRadius: 4 }} />
        <div style={{ position: "absolute", right: 40, top: 104, width: 10, height: 10, borderRadius: 5, background: "#ffffff", boxShadow: "0 0 10px #ffffff" }} />
        <div style={{ position: "absolute", left: 0, right: 0, bottom: -14, height: 14, margin: "0 30px", borderRadius: "0 0 14px 14px", background: "#6b737c" }} />
      </div>
      <div
        style={{
          position: "absolute",
          // Mirrored, so the connector points into the port at (1220, 529).
          top: 529 - 48,
          left: interpolate(frame, [10, plug], [300, 1220 - 300 + 34], {
            ...clamp,
            easing: Easing.bezier(0.5, 0, 0.2, 1),
          }),
          scale: "-1 1",
        }}
      >
        <Dongle width={300} />
      </div>
      <AbsoluteFill style={{ paddingLeft: 140, paddingTop: 640, alignItems: "flex-start" }}>
        <Headline at={plug + 10} size={84} style={{ textAlign: "left" }}>No driver.</Headline>
        <Headline at={plug + 22} size={84} style={{ textAlign: "left" }}>No device.</Headline>
        <Headline at={plug + 34} size={84} color={COLORS.grey} style={{ textAlign: "left" }}>
          Nothing.
        </Headline>
      </AbsoluteFill>
    </Fade>
  );
};
