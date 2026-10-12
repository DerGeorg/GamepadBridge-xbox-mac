import React from "react";
import { AbsoluteFill, interpolate, useCurrentFrame, useVideoConfig } from "remotion";
import { clamp } from "../theme";

/* Every scene fades in and out over a few frames, so cuts never jump. */
export const Fade: React.FC<{ children: React.ReactNode; out?: number }> = ({ children, out = 8 }) => {
  const frame = useCurrentFrame();
  const { durationInFrames } = useVideoConfig();

  return (
    <AbsoluteFill
      style={{
        opacity: interpolate(
          frame,
          [0, 8, durationInFrames - out, durationInFrames],
          [0, 1, 1, 0],
          clamp,
        ),
      }}
    >
      {children}
    </AbsoluteFill>
  );
};
