// Organic.h - umbrella header for the "organic" ImGui framework.
// An ImGui re-imagining of juce_organicui + juce_timeline:
//   - Parameter / Container data model with undo, selection, serialization
//   - Auto-generated Inspector, Outliner, Logger, Media Pool, Scope panels
//   - Dockable window management with named layouts (DockManager)
//   - Timeline editor: clip layers (drag & drop, resize, waveforms),
//     automation curves with easings, gradient tracks, snapping, zoom/pan
#pragma once

#include "OrganicCore.h"
#include "OrganicEasing.h"
#include "OrganicAudio.h"
#include "OrganicAudioEngine.h"
#include "OrganicManager.h"
#include "OrganicCurve2D.h"
#include "OrganicTimeline.h"
#include "OrganicTimelineUI.h"
#include "OrganicPanels.h"
#include "OrganicDock.h"
