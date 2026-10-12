import React from "react";
import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { Backdrop, Dongle, Gamepad, Headline } from "../parts";
import { clamp, COLORS } from "../theme";
import { Fade } from "./Fade";

export const Hook: React.FC = () => {
  const frame = useCurrentFrame();

  return (
    <Fade>
      <Backdrop glow={COLORS.yellow} />
      <AbsoluteFill style={{ alignItems: "center", paddingTop: 170 }}>
        <Headline size={104}>Your Mac ignores</Headline>
        <Headline size={104} at={10} color={COLORS.yellow}>
          your Xbox controller.
        </Headline>
      </AbsoluteFill>
      <AbsoluteFill style={{ alignItems: "center", justifyContent: "flex-end", paddingBottom: 150 }}>
        <div
          style={{
            display: "flex",
            alignItems: "center",
            gap: 90,
            opacity: interpolate(frame, [18, 36], [0, 1], clamp),
            translate: `0px ${Math.sin(frame / 14) * 8}px`,
          }}
        >
          <Gamepad width={560} />
          <Dongle width={300} />
        </div>
      </AbsoluteFill>
    </Fade>
  );
};
