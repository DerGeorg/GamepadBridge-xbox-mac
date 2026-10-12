import React from "react";
import { AbsoluteFill, Easing, interpolate, useCurrentFrame } from "remotion";
import { AppIcon, Backdrop, Headline } from "../parts";
import { clamp, COLORS, CUES, sec } from "../theme";
import { Fade } from "./Fade";

export const Outro: React.FC = () => {
  const frame = useCurrentFrame();
  const hit = sec(CUES.Outro.hit);

  return (
    <Fade out={20}>
      <Backdrop />
      <AbsoluteFill style={{ alignItems: "center", justifyContent: "center", gap: 30 }}>
        <div
          style={{
            scale: interpolate(frame, [hit, hit + 18], [0.6, 1], {
              ...clamp,
              easing: Easing.spring({ damping: 14 }),
              output: "perceptual-scale",
            }),
            opacity: interpolate(frame, [hit, hit + 6], [0, 1], clamp),
          }}
        >
          <AppIcon size={200} style={{ borderRadius: 44 }} />
        </div>
        <Headline at={hit + 8} size={112}>GamepadBridge</Headline>
        <Headline at={hit + 18} size={60} weight={600} color={COLORS.green}>
          gamepadbridge.dergeorg.at
        </Headline>
        <Headline at={hit + 28} size={40} weight={400} color={COLORS.grey}>
          Free and open source · macOS 15 or newer
        </Headline>
      </AbsoluteFill>
    </Fade>
  );
};
