import React from "react";
import {
  AbsoluteFill,
  Easing,
  Img,
  interpolate,
  staticFile,
  useCurrentFrame,
} from "remotion";
import { clamp, COLORS, fontFamily } from "./theme";

const ease = Easing.bezier(0.16, 1, 0.3, 1);

/* The site's look: dark navy with a glow and a faint grid that drifts. */
export const Backdrop: React.FC<{ glow?: string }> = ({ glow = COLORS.green }) => {
  const frame = useCurrentFrame();

  return (
    <AbsoluteFill style={{ backgroundColor: COLORS.black }}>
      <AbsoluteFill
        style={{
          background: `radial-gradient(ellipse 70% 60% at 50% 45%, ${glow}33 0%, ${COLORS.navy} 55%, ${COLORS.black} 100%)`,
        }}
      />
      <AbsoluteFill
        style={{
          opacity: 0.35,
          backgroundImage:
            "linear-gradient(rgba(255,255,255,0.05) 1px, transparent 1px), linear-gradient(90deg, rgba(255,255,255,0.05) 1px, transparent 1px)",
          backgroundSize: "64px 64px",
          backgroundPosition: `${frame * 0.4}px ${frame * 0.25}px`,
        }}
      />
    </AbsoluteFill>
  );
};

/* A headline that slides up and fades in at `at` frames. */
export const Headline: React.FC<{
  at?: number;
  size?: number;
  color?: string;
  weight?: number;
  children: React.ReactNode;
  style?: React.CSSProperties;
}> = ({ at = 0, size = 96, color = COLORS.text, weight = 800, children, style }) => {
  const frame = useCurrentFrame();

  return (
    <div
      style={{
        fontFamily,
        fontSize: size,
        fontWeight: weight,
        color,
        letterSpacing: "-0.02em",
        lineHeight: 1.08,
        textAlign: "center",
        opacity: interpolate(frame, [at, at + 14], [0, 1], { ...clamp, easing: ease }),
        translate: interpolate(frame, [at, at + 18], ["0px 40px", "0px 0px"], {
          ...clamp,
          easing: ease,
        }),
        ...style,
      }}
    >
      {children}
    </div>
  );
};

/* The app icon from packaging/icon, as on the website. */
export const AppIcon: React.FC<{ size: number; style?: React.CSSProperties }> = ({
  size,
  style,
}) => (
  <Img
    src={staticFile("icon.png")}
    style={{ width: size, height: size, ...style }}
  />
);

/*
 * A generic gamepad, drawn - not a picture of anyone's product. `ring` lights
 * one quarter around the centre button, the way an Xbox 360 pad shows its
 * slot; `glow` lights the centre button once it is connected.
 */
export const Gamepad: React.FC<{
  width: number;
  variant?: "one" | "x360";
  glow?: number;
  ring?: number;
  ringSpin?: number;
  pressed?: number;
  style?: React.CSSProperties;
}> = ({ width, variant = "one", glow = 0, ring = -1, ringSpin = -1, pressed = 0, style }) => {
  const body = variant === "x360" ? "#e4e7ea" : "#2a2f36";
  const edge = variant === "x360" ? "#b9c0c7" : "#3a414a";
  const stick = variant === "x360" ? "#3a414a" : "#15181c";

  const quarters = [0, 1, 2, 3].map((q) => {
    const start = -135 + q * 90 + 8;
    const end = start + 74;
    const r = 26;
    const rad = (d: number) => (d * Math.PI) / 180;
    const p = (d: number) => `${200 + r * Math.cos(rad(d))} ${82 + r * Math.sin(rad(d))}`;

    const lit = ring === q || (ringSpin >= 0 && Math.floor(ringSpin) % 4 === q);

    return (
      <path
        key={q}
        d={`M ${p(start)} A ${r} ${r} 0 0 1 ${p(end)}`}
        stroke={lit ? "#57e08f" : "rgba(0,0,0,0.18)"}
        strokeWidth={7}
        strokeLinecap="round"
        fill="none"
        style={{ filter: lit ? "drop-shadow(0 0 6px #57e08f)" : undefined }}
      />
    );
  });

  return (
    <svg viewBox="0 0 400 260" width={width} style={{ overflow: "visible", ...style }}>
      <defs>
        <linearGradient id={`shade-${variant}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="rgba(255,255,255,0.18)" />
          <stop offset="1" stopColor="rgba(0,0,0,0.25)" />
        </linearGradient>
      </defs>
      <path
        d="M80 60 C110 40 160 40 200 48 C240 40 290 40 320 60 C360 85 390 170 380 215 C372 250 335 255 315 230 L285 190 C270 175 250 170 200 170 C150 170 130 175 115 190 L85 230 C65 255 28 250 20 215 C10 170 40 85 80 60 Z"
        fill={body}
        stroke={edge}
        strokeWidth={3}
      />
      <path
        d="M80 60 C110 40 160 40 200 48 C240 40 290 40 320 60 C360 85 390 170 380 215 C372 250 335 255 315 230 L285 190 C270 175 250 170 200 170 C150 170 130 175 115 190 L85 230 C65 255 28 250 20 215 C10 170 40 85 80 60 Z"
        fill={`url(#shade-${variant})`}
      />
      {/* sticks */}
      <circle cx={122} cy={104} r={25} fill={stick} />
      <circle cx={122} cy={104} r={17} fill="#22262b" stroke="#444b54" strokeWidth={2} />
      <circle cx={252} cy={148} r={23} fill={stick} />
      <circle cx={252} cy={148} r={15} fill="#22262b" stroke="#444b54" strokeWidth={2} />
      {/* d-pad */}
      <path d="M140 140 h20 v-16 h16 v16 h20 v16 h-20 v16 h-16 v-16 h-20 z" fill={stick} transform="translate(-6 0)" />
      {/* face buttons */}
      <circle cx={290} cy={78} r={11} fill="#e8b33a" />
      <circle cx={313} cy={101} r={11} fill="#e0524f" />
      <circle cx={290} cy={124} r={11} fill="#4fb35c" style={{ translate: `0px ${pressed * 2}px` }} />
      <circle cx={267} cy={101} r={11} fill="#3f86d9" />
      {/* centre button */}
      {variant === "x360" ? quarters : null}
      <circle
        cx={200}
        cy={82}
        r={17}
        fill={glow > 0 ? `rgba(87, 224, 143, ${0.35 + glow * 0.65})` : stick}
        stroke={variant === "x360" ? "#9aa3ac" : "#4a525c"}
        strokeWidth={2}
        style={{ filter: glow > 0 ? `drop-shadow(0 0 ${8 + glow * 14}px #57e08f)` : undefined }}
      />
    </svg>
  );
};

/* The USB dongle. `on` lights its little LED. */
export const Dongle: React.FC<{ width: number; on?: number }> = ({ width, on = 0 }) => (
  <svg viewBox="0 0 200 64" width={width} style={{ overflow: "visible" }}>
    <rect x={0} y={18} width={46} height={28} rx={3} fill="#b9c0c7" />
    <rect x={8} y={24} width={10} height={6} fill="#6b737c" />
    <rect x={26} y={24} width={10} height={6} fill="#6b737c" />
    <rect x={44} y={6} width={150} height={52} rx={14} fill="#2a2f36" stroke="#3a414a" strokeWidth={2} />
    <circle
      cx={170}
      cy={32}
      r={6}
      fill={on > 0 ? `rgba(240, 244, 246, ${0.4 + on * 0.6})` : "#4a525c"}
      style={{ filter: on > 0 ? "drop-shadow(0 0 8px #ffffff)" : undefined }}
    />
  </svg>
);

/* The gamepad glyph in the menu bar: outline, or filled once connected. */
const BarGlyph: React.FC<{ filled: boolean }> = ({ filled }) => (
  <svg viewBox="0 0 40 26" width={34} height={22}>
    <path
      d="M9 4 C13 2 17 2 20 3 C23 2 27 2 31 4 C35 7 39 16 38 21 C37 25 33 25 31 22 L28 18 C26 17 24 16 20 16 C16 16 14 17 12 18 L9 22 C7 25 3 25 2 21 C1 16 5 7 9 4 Z"
      fill={filled ? COLORS.text : "none"}
      stroke={COLORS.text}
      strokeWidth={2.4}
    />
    {filled ? null : <circle cx={20} cy={9} r={2.4} fill={COLORS.text} />}
  </svg>
);

/* A macOS-style menu bar across the top, with our item on the right. */
export const MenuBar: React.FC<{ iconIn: number; filled: boolean }> = ({ iconIn, filled }) => (
  <div
    style={{
      position: "absolute",
      top: 0,
      left: 0,
      right: 0,
      height: 52,
      background: "rgba(13, 15, 17, 0.72)",
      borderBottom: "1px solid rgba(255,255,255,0.06)",
      display: "flex",
      alignItems: "center",
      justifyContent: "flex-end",
      gap: 34,
      paddingRight: 40,
      fontFamily,
      fontSize: 24,
      color: COLORS.text,
    }}
  >
    <div style={{ opacity: iconIn, scale: `${0.6 + iconIn * 0.4}` }}>
      <BarGlyph filled={filled} />
    </div>
    <div style={{ fontWeight: 600 }}>Mon 12 Oct&nbsp;&nbsp;9:41</div>
  </div>
);

export type MenuLine =
  | { kind: "status"; text: string; key?: string }
  | { kind: "action"; text: string; highlighted?: boolean; checked?: boolean; key?: string }
  | { kind: "separator"; key?: string };

/* The app's menu, as it looks in 1.2.0. Status lines grey, actions white. */
export const Menu: React.FC<{
  lines: MenuLine[];
  open: number;
  right: number;
  appear?: Record<string, number>;
}> = ({ lines, open, right, appear = {} }) => (
  <div
    style={{
      position: "absolute",
      top: 62,
      right,
      width: 610,
      padding: "10px 10px",
      borderRadius: 18,
      background: COLORS.menu,
      border: "1px solid rgba(255,255,255,0.10)",
      boxShadow: "0 30px 80px rgba(0,0,0,0.55)",
      fontFamily,
      fontSize: 30,
      opacity: open,
      scale: `${0.94 + open * 0.06}`,
      transformOrigin: "top right",
    }}
  >
    {lines.map((line, i) => {
      const key = line.key ?? `${i}`;
      const shown = appear[key] ?? 1;

      if (line.kind === "separator") {
        return (
          <div key={key} style={{ height: 1, background: "rgba(255,255,255,0.12)", margin: "8px 14px" }} />
        );
      }

      const highlighted = line.kind === "action" && line.highlighted;

      return (
        <div
          key={key}
          style={{
            height: 54 * shown,
            overflow: "hidden",
            opacity: shown,
            display: "flex",
            alignItems: "center",
            padding: "0 14px",
            borderRadius: 10,
            background: highlighted ? COLORS.green : "transparent",
            color: line.kind === "status" ? COLORS.menuDisabled : COLORS.text,
            fontWeight: highlighted ? 600 : 400,
          }}
        >
          <span style={{ width: 30, display: "inline-block" }}>
            {line.kind === "action" && line.checked ? "✓" : ""}
          </span>
          {line.text}
        </div>
      );
    })}
  </div>
);

/* A macOS-style arrow pointer. */
export const Cursor: React.FC<{ x: number; y: number; press?: number }> = ({ x, y, press = 0 }) => (
  <svg
    viewBox="0 0 24 34"
    width={34}
    style={{
      position: "absolute",
      left: x,
      top: y,
      scale: `${1 - press * 0.12}`,
      filter: "drop-shadow(0 2px 4px rgba(0,0,0,0.5))",
    }}
  >
    <path d="M2 2 L2 28 L9 21 L14 32 L18 30 L13 19 L22 19 Z" fill="#ffffff" stroke="#000" strokeWidth={1.6} strokeLinejoin="round" />
  </svg>
);

/* A small pill like the website's hero badge. */
export const Badge: React.FC<{ children: React.ReactNode; style?: React.CSSProperties }> = ({
  children,
  style,
}) => (
  <div
    style={{
      fontFamily,
      fontSize: 30,
      fontWeight: 600,
      color: COLORS.green,
      border: `2px solid ${COLORS.green}88`,
      background: `${COLORS.green}1f`,
      borderRadius: 999,
      padding: "12px 30px",
      ...style,
    }}
  >
    {children}
  </div>
);
