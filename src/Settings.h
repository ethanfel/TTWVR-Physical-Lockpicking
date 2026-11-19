// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "PinSearch.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
namespace locks {
struct Ergonomics {
  float lockDistance = .42f, lockBelowEyes = .42f;
  float fullTurnWrist = 40.f; // parsed for old INIs; lever input ignores it
  float pinSearchDegrees = 270.f;
  bool nativeScene = false;
  bool useTargetObject = true;
  bool useGameProps = true, useGameBackground = true;
  float backgroundBlur = 12.f, backgroundBrightness = .35f;
  bool requirePreparation = true, useHolders = true, dropPins = true;
  bool freeDriver = true;
  float holderSide[2]{-.22f, .22f}, holderBelowEyes[2]{.60f, .60f},
      holderForward[2]{.16f, .16f};
  float handConstraint = .035f, handAngle = 20.f;
  float insertRadius = .028f, insertionTravel = .018f, withdrawTravel = .055f;
  void load(const std::string &text) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
      auto comment = line.find_first_of(";#");
      if (comment != std::string::npos)
        line.resize(comment);
      auto equal = line.find('=');
      if (equal == std::string::npos)
        continue;
      std::string key = line.substr(0, equal);
      key.erase(std::remove_if(
                    key.begin(), key.end(),
                    [](char c) { return c == ' ' || c == '\t' || c == '\r'; }),
                key.end());
      float value;
      std::istringstream number(line.substr(equal + 1));
      if (!(number >> value) || !std::isfinite(value))
        continue;
      if (key == "NativeSceneRendering")
        nativeScene = value >= .5f;
      if (key == "LockDistanceMeters")
        lockDistance = std::clamp(value, .25f, .65f);
      if (key == "LockBelowEyesMeters")
        lockBelowEyes = std::clamp(value, .20f, .70f);
      if (key == "FullTurnWristDegrees")
        fullTurnWrist = std::clamp(value, 20.f, 90.f);
      if (key == "PinSearchDegrees")
        pinSearchDegrees = pinSearchSpan(value);
      if (key == "FreeScrewdriver")
        freeDriver = value >= .5f;
      if (key == "RequirePinPreparation")
        requirePreparation = value >= .5f;
      if (key == "DropPinsToWorld")
        dropPins = value >= .5f;
      if (key == "UseGameBackground")
        useGameBackground = value >= .5f;
      if (key == "BackgroundBlurPixels")
        backgroundBlur = std::clamp(value, 0.f, 32.f);
      if (key == "BackgroundBrightness")
        backgroundBrightness = std::clamp(value, 0.f, 1.f);
      if (key == "UseTargetObject")
        useTargetObject = value >= .5f;
      if (key == "UseGameProps")
        useGameProps = value >= .5f;
      if (key == "UseToolHolders")
        useHolders = value >= .5f;
      if (key == "HandConstraintMeters")
        handConstraint = std::clamp(value, 0.f, .04f);
      if (key == "HandConstraintDegrees")
        handAngle = std::clamp(value, 0.f, 25.f);
      for (int k = 0; k < 2; k++) {
        std::string prefix = k == 0 ? "PinHolder" : "DriverHolder";
        if (key == prefix + "SideMeters")
          holderSide[k] = std::clamp(value, -.45f, .45f);
        if (key == prefix + "BelowEyesMeters")
          holderBelowEyes[k] = std::clamp(value, .30f, .90f);
        if (key == prefix + "ForwardMeters")
          holderForward[k] = std::clamp(value, .08f, .40f);
      }
      if (key == "InsertRadiusMeters")
        insertRadius = std::clamp(value, .015f, .05f);
      if (key == "InsertionTravelMeters")
        insertionTravel = std::clamp(value, .01f, .035f);
      if (key == "WithdrawTravelMeters")
        withdrawTravel = std::clamp(value, .04f, .10f);
    }
  }
};
} // namespace locks
