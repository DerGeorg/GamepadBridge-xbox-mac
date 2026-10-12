import { loadFont } from "@remotion/google-fonts/Inter";
import timeline from "./timeline.json";

// The website's dergeorg brand, dark variant.
export const COLORS = {
  navy: "#1b1f24",
  black: "#0d0f11",
  green: "#2bbd84",
  yellow: "#fbb040",
  grey: "#a8b0b8",
  text: "#e9ecef",
  menu: "rgba(40, 44, 50, 0.92)",
  menuDisabled: "rgba(233, 236, 239, 0.42)",
};

export const { fontFamily } = loadFont("normal", {
  weights: ["400", "600", "800"],
  subsets: ["latin"],
});

export const FPS = timeline.fps;
export const CUES = timeline.cues;

// Frames from seconds, rounded to whole frames.
export const sec = (seconds: number) => Math.round(seconds * FPS);

export const sceneFrames = (id: string) => {
  const scene = timeline.scenes.find((s) => s.id === id);

  if (!scene) {
    throw new Error(`No scene ${id} in timeline.json`);
  }

  return sec(scene.seconds);
};

export const TOTAL_FRAMES = timeline.scenes.reduce(
  (sum, scene) => sum + sec(scene.seconds),
  0,
);

// Clamped on both ends, which is what almost every animation here wants.
export const clamp = {
  extrapolateLeft: "clamp",
  extrapolateRight: "clamp",
} as const;
