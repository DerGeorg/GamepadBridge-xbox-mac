import React from "react";
import { AbsoluteFill, Easing, interpolate, useCurrentFrame } from "remotion";
import { AppIcon, Backdrop, Badge, Headline } from "../parts";
import { clamp, CUES, sec } from "../theme";
import { Fade } from "./Fade";

export const Reveal: React.FC = () => {
  const frame = useCurrentFrame();
  const hit = sec(CUES.Reveal.hit);

  return (
    <Fade>
      <Backdrop />
      <AbsoluteFill style={{ alignItems: "center", justifyContent: "center", gap: 36 }}>
        <div
          style={{
            scale: interpolate(frame, [hit, hit + 20], [0.4, 1], {
              ...clamp,
              easing: Easing.spring({ damping: 12 }),
              output: "perceptual-scale",
            }),
            opacity: interpolate(frame, [hit, hit + 6], [0, 1], clamp),
            filter: "drop-shadow(0 30px 60px rgba(43,189,132,0.35))",
          }}
        >
          <AppIcon size={260} style={{ borderRadius: 58 }} />
        </div>
        <Headline at={hit + 14} size={128}>GamepadBridge</Headline>
        <div style={{ opacity: interpolate(frame, [hit + 34, hit + 48], [0, 1], clamp) }}>
          <Badge>Xbox One &amp; Xbox 360 · free · open source</Badge>
        </div>
      </AbsoluteFill>
    </Fade>
  );
};
