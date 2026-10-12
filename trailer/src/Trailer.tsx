import React from "react";
import { Series, staticFile } from "remotion";
import { Audio } from "@remotion/media";
import { sceneFrames } from "./theme";
import { Hook } from "./scenes/Hook";
import { Problem } from "./scenes/Problem";
import { Reveal } from "./scenes/Reveal";
import { Install } from "./scenes/Install";
import { Pair } from "./scenes/Pair";
import { Xbox360 } from "./scenes/Xbox360";
import { Features } from "./scenes/Features";
import { Outro } from "./scenes/Outro";

/*
 * The scenes run back to back; each fades in and out by itself. Their
 * lengths and every sound cue come from timeline.json, which
 * audio/make_audio.py reads too - change a time there, then regenerate the
 * sound, and picture and sound stay together.
 */
export const Trailer: React.FC = () => (
  <>
    <Series>
      <Series.Sequence name="Hook" durationInFrames={sceneFrames("Hook")}>
        <Hook />
      </Series.Sequence>
      <Series.Sequence name="Problem" durationInFrames={sceneFrames("Problem")}>
        <Problem />
      </Series.Sequence>
      <Series.Sequence name="Reveal" durationInFrames={sceneFrames("Reveal")}>
        <Reveal />
      </Series.Sequence>
      <Series.Sequence name="Install" durationInFrames={sceneFrames("Install")}>
        <Install />
      </Series.Sequence>
      <Series.Sequence name="Pair" durationInFrames={sceneFrames("Pair")}>
        <Pair />
      </Series.Sequence>
      <Series.Sequence name="Xbox360" durationInFrames={sceneFrames("Xbox360")}>
        <Xbox360 />
      </Series.Sequence>
      <Series.Sequence name="Features" durationInFrames={sceneFrames("Features")}>
        <Features />
      </Series.Sequence>
      <Series.Sequence name="Outro" durationInFrames={sceneFrames("Outro")}>
        <Outro />
      </Series.Sequence>
    </Series>
    <Audio src={staticFile("trailer-audio.wav")} />
  </>
);
