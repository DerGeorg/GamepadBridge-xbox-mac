import React from "react";
import { AbsoluteFill, Easing, interpolate, useCurrentFrame } from "remotion";
import { Backdrop, Headline } from "../parts";
import { clamp, COLORS, CUES, fontFamily, sec } from "../theme";
import { Fade } from "./Fade";

const CARDS = [
  { title: "Several at once", text: "Every controller its own pad" },
  { title: "Rumble", text: "From macOS and from games" },
  { title: "The Xbox button", text: "Opens the Games app, like a real pad" },
  { title: "Signed & notarized", text: "Updates itself, opens at login" },
];

export const Features: React.FC = () => {
  const frame = useCurrentFrame();

  return (
    <Fade>
      <Backdrop />
      <AbsoluteFill style={{ alignItems: "center", paddingTop: 130 }}>
        <Headline size={84}>Everything a controller should do.</Headline>
      </AbsoluteFill>
      <AbsoluteFill style={{ alignItems: "center", justifyContent: "center", paddingTop: 170 }}>
        <div style={{ display: "grid", gridTemplateColumns: "640px 640px", gap: 36 }}>
          {CARDS.map((card, i) => {
            const at = sec(CUES.Features.cards[i]);

            return (
              <div
                key={card.title}
                style={{
                  borderRadius: 28,
                  padding: "40px 44px",
                  background: "rgba(255,255,255,0.05)",
                  border: `2px solid ${COLORS.green}55`,
                  fontFamily,
                  opacity: interpolate(frame, [at, at + 8], [0, 1], clamp),
                  scale: interpolate(frame, [at, at + 14], [0.85, 1], {
                    ...clamp,
                    easing: Easing.spring({ damping: 14 }),
                    output: "perceptual-scale",
                  }),
                }}
              >
                <div style={{ fontSize: 52, fontWeight: 800, color: COLORS.text }}>
                  <span style={{ color: COLORS.green }}>✓</span>&nbsp;{card.title}
                </div>
                <div style={{ fontSize: 34, color: COLORS.grey, marginTop: 10 }}>{card.text}</div>
              </div>
            );
          })}
        </div>
      </AbsoluteFill>
    </Fade>
  );
};
