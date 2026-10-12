import React from "react";
import { Composition, Folder } from "remotion";
import { FPS, sceneFrames, TOTAL_FRAMES } from "./theme";
import { Trailer } from "./Trailer";
import { Hook } from "./scenes/Hook";
import { Problem } from "./scenes/Problem";
import { Reveal } from "./scenes/Reveal";
import { Install } from "./scenes/Install";
import { Pair } from "./scenes/Pair";
import { Xbox360 } from "./scenes/Xbox360";
import { Features } from "./scenes/Features";
import { Outro } from "./scenes/Outro";

const SCENES = { Hook, Problem, Reveal, Install, Pair, Xbox360, Features, Outro };

export const RemotionRoot: React.FC = () => (
  <>
    <Folder name="Trailer-Scenes">
      {Object.entries(SCENES).map(([id, component]) => (
        <Composition
          key={id}
          id={id}
          component={component}
          width={1920}
          height={1080}
          fps={FPS}
          durationInFrames={sceneFrames(id)}
        />
      ))}
    </Folder>
    <Composition
      id="Trailer"
      component={Trailer}
      width={1920}
      height={1080}
      fps={FPS}
      durationInFrames={TOTAL_FRAMES}
    />
  </>
);
