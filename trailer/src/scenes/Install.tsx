import React from "react";
import { AbsoluteFill, Easing, interpolate, useCurrentFrame } from "remotion";
import { AppIcon, Backdrop, Cursor, Headline } from "../parts";
import { clamp, COLORS, CUES, fontFamily, sec } from "../theme";
import { Fade } from "./Fade";

const move = { ...clamp, easing: Easing.bezier(0.45, 0, 0.2, 1) };

const Folder: React.FC<{ bounce: number }> = ({ bounce }) => (
  <svg viewBox="0 0 200 160" width={200} style={{ scale: `${1 + bounce * 0.08}` }}>
    <path d="M10 30 Q10 18 22 18 H78 L92 34 H178 Q190 34 190 46 V140 Q190 152 178 152 H22 Q10 152 10 140 Z" fill="#3f86d9" />
    <path d="M10 52 Q10 44 22 44 H178 Q190 44 190 56 V140 Q190 152 178 152 H22 Q10 152 10 140 Z" fill="#5ba0ec" />
    <path d="M78 122 L100 72 L122 122 M86 106 H114" stroke="#ffffff" strokeWidth={9} strokeLinecap="round" strokeLinejoin="round" fill="none" opacity={0.9} />
  </svg>
);

const Toggle: React.FC<{ label: string; on: number }> = ({ label, on }) => (
  <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", padding: "22px 28px" }}>
    <span style={{ fontFamily, fontSize: 32, color: COLORS.text }}>{label}</span>
    <div
      style={{
        width: 84,
        height: 48,
        borderRadius: 24,
        background: on > 0.5 ? COLORS.green : "#4a525c",
        position: "relative",
      }}
    >
      <div
        style={{
          position: "absolute",
          top: 4,
          left: 4 + on * 36,
          width: 40,
          height: 40,
          borderRadius: 20,
          background: "#fff",
        }}
      />
    </div>
  </div>
);

export const Install: React.FC = () => {
  const frame = useCurrentFrame();
  const c = CUES.Install;
  const grab = sec(c.grab);
  const drop = sec(c.drop);
  const p1 = sec(c.permission1);
  const p2 = sec(c.permission2);

  // The icon travels from its slot to the Applications folder.
  // While the window stands centred (left 530): its slot to its folder.
  const iconX = interpolate(frame, [grab, drop], [690, 1180], move);
  const iconY = interpolate(frame, [grab, (grab + drop) / 2, drop], [470, 410, 470], move);
  const iconScale = interpolate(frame, [drop - 4, drop + 6], [1, 0.3], clamp);
  const iconGone = interpolate(frame, [drop, drop + 6], [1, 0], clamp);

  const step2 = sec(c.permission1) - 12;

  // Centred while it is the only thing on screen, then making room.
  const windowLeft = interpolate(frame, [step2 - 4, step2 + 12], [530, 260], move);

  return (
    <Fade>
      <Backdrop />
      <AbsoluteFill style={{ alignItems: "center", paddingTop: 110 }}>
        <div style={{ position: "absolute", top: 110, opacity: interpolate(frame, [step2 - 8, step2], [1, 0], clamp) }}>
          <Headline size={84}>
            <span style={{ color: COLORS.green }}>1</span>&nbsp; Drag it to Applications
          </Headline>
        </div>
        <div style={{ position: "absolute", top: 110 }}>
          <Headline at={step2} size={84}>
            <span style={{ color: COLORS.green }}>2</span>&nbsp; Allow two permissions
          </Headline>
        </div>
      </AbsoluteFill>

      {/* The disk image window. */}
      <div
        style={{
          position: "absolute",
          left: windowLeft,
          top: 300,
          width: 860,
          height: 480,
          borderRadius: 24,
          background: "#23272d",
          border: "1px solid rgba(255,255,255,0.1)",
          boxShadow: "0 40px 90px rgba(0,0,0,0.5)",
          overflow: "hidden",
        }}
      >
        <div style={{ height: 52, background: "#2c3138", display: "flex", alignItems: "center", gap: 10, paddingLeft: 20 }}>
          {["#ff5f57", "#febc2e", "#28c840"].map((col) => (
            <div key={col} style={{ width: 16, height: 16, borderRadius: 8, background: col }} />
          ))}
          <span style={{ fontFamily, fontSize: 22, color: COLORS.grey, marginLeft: 260 }}>GamepadBridge</span>
        </div>
        <svg width={860} height={428} style={{ position: "absolute", top: 52, left: 0 }}>
          <path d="M330 210 H520" stroke={COLORS.grey} strokeWidth={6} strokeDasharray="14 12" strokeLinecap="round" />
          <path d="M505 190 L530 210 L505 230" stroke={COLORS.grey} strokeWidth={6} fill="none" strokeLinecap="round" strokeLinejoin="round" />
        </svg>
        <div style={{ position: "absolute", left: 560, top: 182 }}>
          <Folder bounce={interpolate(frame, [drop, drop + 5, drop + 14], [0, 1, 0], clamp)} />
          <div style={{ fontFamily, fontSize: 24, color: COLORS.text, textAlign: "center" }}>Applications</div>
        </div>
      </div>

      {/* The app icon, then the cursor carrying it. */}
      <div style={{ position: "absolute", left: iconX - 100, top: iconY - 100, opacity: iconGone, scale: `${iconScale}` }}>
        <AppIcon size={200} style={{ borderRadius: 44 }} />
      </div>
      <Cursor
        x={interpolate(frame, [0, grab, drop, drop + 20], [700, iconX + 10, iconX + 10, 1000], move)}
        y={interpolate(frame, [0, grab, drop, drop + 20], [760, iconY + 10, iconY + 10, 820], move)}
        press={frame >= grab && frame <= drop ? 1 : 0}
      />

      {/* The permission window. */}
      <div
        style={{
          position: "absolute",
          left: 1220,
          top: 360,
          width: 560,
          borderRadius: 24,
          background: "#23272d",
          border: "1px solid rgba(255,255,255,0.1)",
          boxShadow: "0 40px 90px rgba(0,0,0,0.5)",
          opacity: interpolate(frame, [step2, step2 + 10], [0, 1], clamp),
          translate: interpolate(frame, [step2, step2 + 14], ["0px 40px", "0px 0px"], move),
        }}
      >
        <Toggle label="Input Monitoring" on={interpolate(frame, [p1, p1 + 6], [0, 1], clamp)} />
        <div style={{ height: 1, background: "rgba(255,255,255,0.1)", margin: "0 28px" }} />
        <Toggle label="Accessibility" on={interpolate(frame, [p2, p2 + 6], [0, 1], clamp)} />
      </div>
    </Fade>
  );
};
