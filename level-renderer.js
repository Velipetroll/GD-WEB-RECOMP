class LevelRenderer {
  constructor(value71, other) {
    ((this["_scene"] = value71),
      (this["_cameraXRef"] = other),
      (this["additiveContainer"] = value71["add"]
        ["container"](0, 0)
        ["setDepth"](-1)),
      (this["container"] = value71["add"]["container"](0, 0)),
      (this["topContainer"] = value71["add"]
        ["container"](0, 0)
        ["setDepth"](13)),
      (this["objects"] = []),
      (this["endXPos"] = 0),
      (this["_groundY"] = 0),
      (this["_ceilingY"] = null),
      (this["_flyGroundActive"] = !1),
      (this["_groundAnimFrom"] = 0),
      (this["_groundAnimTo"] = 0),
      (this["_groundAnimTime"] = 0),
      (this["_groundAnimDuration"] = 0),
      (this["_groundAnimating"] = !1),
      (this["_groundTargetValue"] = 0),
      (this["_flyFloorY"] = 0),
      (this["_flyCeilingY"] = 0),
      (this["flyCameraTarget"] = null),
      (this["_colorTriggers"] = []),
      (this["_colorTriggerIdx"] = 0),
      (this["_audioScaleSprites"] = []),
      (this["_enterEffectTriggers"] = []),
      (this["_enterEffectTriggerIdx"] = 0),
      (this["_activeEnterEffect"] = 0),
      (this["_activeExitEffect"] = 0),
      (this["_sections"] = []),
      (this["_sectionContainers"] = []),
      (this["_collisionSections"] = []),
      (this["_nearbyBuffer"] = []),
      (this["_visMinSec"] = -1),
      (this["_visMaxSec"] = -1),
      (this["_groundStartScreenY"] = flipY(0)),
      (this["_ceilingStartScreenY"] = 0),
      this["_buildGround"]());
  }
  ["loadLevel"](value71) {
    let { objects: helperFn172 } = helperFn17(value71);
    this["_spawnLevelObjects"](helperFn172);
  }
  ["_buildGround"]() {
    const scene = this["_scene"],
      value71 = scene["textures"]["getFrame"](
        "GJ_WebSheet",
        "groundSquare_01_001.png",
      );
    ((this["_tileW"] = value71 ? value71["width"] : 1012),
      (this["_groundTiles"] = []),
      (this["_ceilingTiles"] = []));
    let value72 = Math["ceil"](screenWidth / this["_tileW"]) + 2,
      helperFn22 = flipY(0);
    const value73 = -groundYOffset;
    for (let i = 0; i < value72; i++) {
      let value78 = value73 + i * this["_tileW"],
        image = scene["add"]["image"](
          0,
          helperFn22,
          "GJ_WebSheet",
          "groundSquare_01_001.png",
        );
      (image["setOrigin"](0, 0),
        image["setTint"](17578),
        image["setDepth"](20),
        (image["_worldX"] = value78),
        this["_groundTiles"]["push"](image));
      let image2 = scene["add"]["image"](
        0,
        helperFn22,
        "GJ_WebSheet",
        "groundSquare_01_001.png",
      );
      (image2["setOrigin"](0, 1),
        image2["setFlipY"](!0),
        image2["setTint"](17578),
        image2["setDepth"](20),
        image2["setVisible"](!1),
        (image2["_worldX"] = value78),
        this["_ceilingTiles"]["push"](image2));
    }
    this["_maxGroundWorldX"] = value73 + (value72 - 1) * this["_tileW"];
    const value74 = scene["textures"]["getFrame"](
        "GJ_WebSheet",
        "floorLine_01_001.png",
      ),
      value75 = value74 ? value74["width"] : 888,
      value76 = screenWidth / value75;
    ((this["_groundLine"] = scene["add"]
      ["image"](
        screenWidth / 2,
        helperFn22 - 1,
        "GJ_WebSheet",
        "floorLine_01_001.png",
      )
      ["setOrigin"](0.5, 0)
      ["setScale"](value76, 1)
      ["setBlendMode"](blendAdd)
      ["setDepth"](21)
      ["setScrollFactor"](0)),
      (this["_ceilingLine"] = scene["add"]
        ["image"](
          screenWidth / 2,
          helperFn22 + 1,
          "GJ_WebSheet",
          "floorLine_01_001.png",
        )
        ["setOrigin"](0.5, 1)
        ["setScale"](value76, 1)
        ["setFlipY"](!0)
        ["setBlendMode"](blendAdd)
        ["setDepth"](21)
        ["setScrollFactor"](0)
        ["setVisible"](!1)));
    const value77 = 100 / 255;
    ((this["_groundShadowL"] = scene["add"]
      ["image"](-1, helperFn22, "GJ_WebSheet", "groundSquareShadow_001.png")
      ["setOrigin"](0, 0)
      ["setScrollFactor"](0)
      ["setDepth"](22)
      ["setAlpha"](value77)
      ["setScale"](0.7, 1)
      ["setBlendMode"](blendNormal)),
      (this["_groundShadowR"] = scene["add"]
        ["image"](
          screenWidth + 1,
          helperFn22,
          "GJ_WebSheet",
          "groundSquareShadow_001.png",
        )
        ["setOrigin"](1, 0)
        ["setScrollFactor"](0)
        ["setDepth"](22)
        ["setAlpha"](value77)
        ["setScale"](0.7, 1)
        ["setFlipX"](!0)
        ["setBlendMode"](blendNormal)),
      (this["_ceilingShadowL"] = scene["add"]
        ["image"](-1, helperFn22, "GJ_WebSheet", "groundSquareShadow_001.png")
        ["setOrigin"](0, 1)
        ["setScrollFactor"](0)
        ["setDepth"](22)
        ["setAlpha"](value77)
        ["setScale"](0.7, 1)
        ["setFlipY"](!0)
        ["setBlendMode"](blendNormal)
        ["setVisible"](!1)),
      (this["_ceilingShadowR"] = scene["add"]
        ["image"](
          screenWidth + 1,
          helperFn22,
          "GJ_WebSheet",
          "groundSquareShadow_001.png",
        )
        ["setOrigin"](1, 1)
        ["setScrollFactor"](0)
        ["setDepth"](22)
        ["setAlpha"](value77)
        ["setScale"](0.7, 1)
        ["setFlipX"](!0)
        ["setFlipY"](!0)
        ["setBlendMode"](blendNormal)
        ["setVisible"](!1)));
  }
  ["resizeScreen"]() {
    var value71, value72;
    const scene = this["_scene"],
      tileW = this["_tileW"],
      value73 = Math["ceil"](screenWidth / tileW) + 2,
      helperFn22 = flipY(0);
    for (; this["_groundTiles"]["length"] < value73;) {
      const value76 = this["_maxGroundWorldX"] + tileW;
      let image = scene["add"]["image"](
        0,
        helperFn22,
        "GJ_WebSheet",
        "groundSquare_01_001.png",
      );
      (image["setOrigin"](0, 0)
        ["setTint"](
          (null == (value71 = this["_groundTiles"][0])
            ? void 0
            : value71["tintTopLeft"]) || 17578,
        )
        ["setDepth"](20),
        (image["_worldX"] = value76),
        this["_groundTiles"]["push"](image));
      let image2 = scene["add"]["image"](
        0,
        helperFn22,
        "GJ_WebSheet",
        "groundSquare_01_001.png",
      );
      (image2["setOrigin"](0, 1)
        ["setFlipY"](!0)
        ["setTint"](
          (null == (value72 = this["_groundTiles"][0])
            ? void 0
            : value72["tintTopLeft"]) || 17578,
        )
        ["setDepth"](20)
        ["setVisible"](!1),
        (image2["_worldX"] = value76),
        this["_ceilingTiles"]["push"](image2),
        (this["_maxGroundWorldX"] = value76));
    }
    const value74 = this["_scene"]["textures"]["getFrame"](
        "GJ_WebSheet",
        "floorLine_01_001.png",
      ),
      value75 = screenWidth / (value74 ? value74["width"] : 888);
    ((this["_groundLine"]["x"] = screenWidth / 2),
      this["_groundLine"]["setScale"](value75, 1),
      (this["_ceilingLine"]["x"] = screenWidth / 2),
      this["_ceilingLine"]["setScale"](value75, 1),
      (this["_groundShadowR"]["x"] = screenWidth + 1),
      (this["_ceilingShadowR"]["x"] = screenWidth + 1));
  }
  ["updateGroundTiles"](value71 = 0) {
    const value72 = this["_cameraXRef"]["value"],
      tileW = this["_tileW"];
    let value73,
      value74,
      value75 = this["_maxGroundWorldX"] || -1 / 0;
    if (this["_flyGroundActive"] && this["_groundTargetValue"] > 0.001) {
      let groundTargetValue = this["_groundTargetValue"],
        num32 = 620,
        num33 = 20;
      ((value73 =
        this["_groundStartScreenY"] +
        (num32 - this["_groundStartScreenY"]) * groundTargetValue),
        (value74 =
          this["_ceilingStartScreenY"] +
          (num33 - this["_ceilingStartScreenY"]) * groundTargetValue));
      let value77 = flipY(0) + value71;
      value73 > value77 && (value73 = value77);
    } else ((value73 = flipY(0) + value71), (value74 = 0));
    for (let i = 0; i < this["_groundTiles"]["length"]; i++) {
      let i2 = this["_groundTiles"][i],
        i3 = this["_ceilingTiles"][i];
      i2["_worldX"] + tileW <= value72 &&
        ((i2["_worldX"] = value75 + tileW),
        (i3["_worldX"] = i2["_worldX"]),
        (value75 = i2["_worldX"]),
        (this["_maxGroundWorldX"] = value75));
      let value77 = i2["_worldX"] - value72;
      ((i2["x"] = value77),
        (i2["y"] = value73),
        (i3["x"] = value77),
        (i3["y"] = value74),
        i3["setVisible"](
          this["_flyGroundActive"] && this["_groundTargetValue"] > 0,
        ));
    }
    ((this["_groundLine"]["y"] = value73),
      this["_flyGroundActive"] && this["_groundTargetValue"] > 0
        ? ((this["_ceilingLine"]["y"] = value74),
          this["_ceilingLine"]["setVisible"](!0))
        : this["_ceilingLine"]["setVisible"](!1),
      (this["_groundShadowL"]["y"] = value73),
      (this["_groundShadowR"]["y"] = value73));
    let value76 = this["_flyGroundActive"] && this["_groundTargetValue"] > 0;
    ((this["_ceilingShadowL"]["y"] = value74),
      (this["_ceilingShadowR"]["y"] = value74),
      this["_ceilingShadowL"]["setVisible"](value76),
      this["_ceilingShadowR"]["setVisible"](value76));
  }
  ["shiftGroundTiles"](value71) {
    for (let i = 0; i < this["_groundTiles"]["length"]; i++)
      ((this["_groundTiles"][i]["_worldX"] += value71),
        (this["_ceilingTiles"][i]["_worldX"] += value71));
    this["_maxGroundWorldX"] += value71;
  }
  ["resetGroundTiles"](value71) {
    const tileW = this["_tileW"];
    for (let i = 0; i < this["_groundTiles"]["length"]; i++)
      ((this["_groundTiles"][i]["_worldX"] = value71 + i * tileW),
        (this["_ceilingTiles"][i]["_worldX"] = value71 + i * tileW));
    ((this["_maxGroundWorldX"] =
      value71 + (this["_groundTiles"]["length"] - 1) * tileW),
      this["resetGroundState"]());
  }
  ["resetGroundState"]() {
    ((this["_flyGroundActive"] = !1),
      (this["_groundTargetValue"] = 0),
      (this["_groundAnimating"] = !1),
      (this["_groundY"] = 0),
      (this["_ceilingY"] = null),
      (this["flyCameraTarget"] = null));
  }
  ["_computeFlyBounds"](value71) {
    let value72 = value71 - 300;
    return (
      (value72 = Math["floor"](value72 / baseUnit) * baseUnit),
      (value72 = Math["max"](0, value72)),
      {
        floorY: value72,
        ceilingY: value72 + physicsConst600,
      }
    );
  }
  ["setFlyMode"](value71, other) {
    if (value71) {
      let value72 = this["_computeFlyBounds"](other);
      ((this["_flyFloorY"] = value72["floorY"]),
        (this["_flyCeilingY"] = value72["ceilingY"]),
        (this["_flyGroundActive"] = !0));
      let value73 = this["_flyFloorY"] + 300;
      ((this["flyCameraTarget"] = value73 - 320 + unusedConst180),
        this["flyCameraTarget"] < 0 && (this["flyCameraTarget"] = 0));
      let value74 = (this["_scene"] && this["_scene"]["_cameraY"]) || 0;
      ((this["_groundStartScreenY"] = flipY(0) + value74),
        (this["_ceilingStartScreenY"] = 0),
        (this["_groundAnimFrom"] = this["_groundTargetValue"]),
        (this["_groundAnimTo"] = 1),
        (this["_groundAnimTime"] = 0),
        (this["_groundAnimDuration"] = 0.5),
        (this["_groundAnimating"] = !0));
    } else
      ((this["flyCameraTarget"] = null),
        (this["_groundAnimFrom"] = this["_groundTargetValue"]),
        (this["_groundAnimTo"] = 0),
        (this["_groundAnimTime"] = 0),
        (this["_groundAnimDuration"] = 0.5),
        (this["_groundAnimating"] = !0));
  }
  ["stepGroundAnimation"](value71) {
    if (!this["_groundAnimating"]) return;
    this["_groundAnimTime"] += value71;
    let value72 =
      this["_groundAnimDuration"] > 0
        ? Math["min"](this["_groundAnimTime"] / this["_groundAnimDuration"], 1)
        : 1;
    ((this["_groundTargetValue"] =
      this["_groundAnimFrom"] +
      (this["_groundAnimTo"] - this["_groundAnimFrom"]) * value72),
      value72 >= 1 &&
        ((this["_groundAnimating"] = !1),
        (this["_groundTargetValue"] = this["_groundAnimTo"]),
        0 === this["_groundAnimTo"] && (this["_flyGroundActive"] = !1)));
  }
  ["getFloorY"]() {
    return this["_flyGroundActive"] ? this["_flyFloorY"] : 0;
  }
  ["getCeilingY"]() {
    return this["_flyGroundActive"] ? this["_flyCeilingY"] : null;
  }
  ["_applyVisualProps"](value71, gameObject, extra, extra2, value72 = null) {
    if (!gameObject) return;
    let { dx: value73, dy: value74 } = (function (value76, other) {
      let helperFn42 = findAtlasFrame(value76, other);
      if (!helperFn42)
        return {
          dx: 0,
          dy: 0,
        };
      let value77 = value76["textures"]
        ["get"](helperFn42["atlas"])
        ["get"](helperFn42["frame"]);
      if (!value77)
        return {
          dx: 0,
          dy: 0,
        };
      let value78 = value77["customData"] || {};
      if (value78["gjSpriteOffset"])
        return {
          dx: value78["gjSpriteOffset"]["x"] || 0,
          dy: -(value78["gjSpriteOffset"]["y"] || 0),
        };
      let realWidth = value77["realWidth"],
        realHeight = value77["realHeight"],
        width = value77["width"],
        height = value77["height"],
        num32 = 0,
        num33 = 0;
      return (
        value78["spriteSourceSize"] &&
          ((num32 = value78["spriteSourceSize"]["x"] || 0),
          (num33 = value78["spriteSourceSize"]["y"] || 0)),
        {
          dx: realWidth / 2 - (num32 + width / 2),
          dy: realHeight / 2 - (num33 + height / 2),
        }
      );
    })(value71, extra);
    (extra2["flipX"] && gameObject["setFlipX"](!0),
      extra2["flipY"] && gameObject["setFlipY"](!0));
    let value75 =
      (gameObject["getData"]("gjBaseRotationDeg") || 0) + extra2["rot"];
    (0 !== value75 && gameObject["setAngle"](value75),
      1 !== extra2["scale"] && gameObject["setScale"](extra2["scale"]),
      value72 &&
        (void 0 !== value72["tint"]
          ? gameObject["setTint"](value72["tint"])
          : value72["black"] && gameObject["setTint"](0)));
  }
  ["_addVisualSprite"](gameObject, value71 = null) {
    gameObject &&
      (value71 && "additive" === value71["blend"]
        ? (gameObject["setBlendMode"](blendAdd), (gameObject["_eeLayer"] = 0))
        : value71 && value71["_portalFront"]
          ? (gameObject["_eeLayer"] = 2)
          : value71 && void 0 !== value71["z"] && value71["z"] < 0
            ? (gameObject["_eeLayer"] = 0)
            : (gameObject["_eeLayer"] = 1));
  }
  ["_getGlowFrameName"](value71) {
    return value71 && value71["endsWith"]("_001.png")
      ? value71["replace"]("_001.png", "_glow_001.png")
      : null;
  }
  ["_addGlowSprite"](value71, other, extra, extra2, extra3, extra4) {
    let value72 = this["_getGlowFrameName"](extra2);
    if (!value72) return;
    if (
      !findAtlasFrame(value71, value72) &&
      !value71["textures"]["exists"](value72)
    )
      return;
    let helperFn52 = addImageFromAtlas(value71, other, extra, value72);
    helperFn52 &&
      (this["_applyVisualProps"](value71, helperFn52, value72, extra3),
      helperFn52["setBlendMode"](blendAdd),
      (helperFn52["_eeLayer"] = 0),
      void 0 !== extra4 &&
        ((helperFn52["_eeWorldX"] = extra4),
        (helperFn52["_eeBaseY"] = extra),
        this["_addToSection"](helperFn52)));
  }
  ["_spawnLevelObjects"](value71) {
    const scene = this["_scene"];
    let Set2 = new Set();
    this["_lastObjectX"] = 0;
    for (let item of value71) {
      let helperFn182 = helperFn18(item["id"]);
      if (helperFn182 && helperFn182["type"] === trigger) {
        ((29 !== item["id"] && 30 !== item["id"]) ||
          this["_colorTriggers"]["push"]({
            x: 2 * item["x"],
            index: 29 === item["id"] ? 1e3 : 1001,
            color: {
              r: parseInt(item["_raw"][7] ?? 255, 10),
              g: parseInt(item["_raw"][8] ?? 255, 10),
              b: parseInt(item["_raw"][9] ?? 255, 10),
            },
            duration: parseFloat(item["_raw"][10] ?? 0),
            tintGround: "1" === item["_raw"][14],
          }),
          helperFn182["enterEffect"] &&
            this["_enterEffectTriggers"]["push"]({
              x: 2 * item["x"],
              effect: helperFn182["enterEffect"],
            }));
        continue;
      }
      let value72 = 2 * item["x"],
        value73 = 2 * item["y"];
      value72 > this["_lastObjectX"] && (this["_lastObjectX"] = value72);
      let value74 = helperFn182 ? helperFn182["frame"] : null;
      if (
        (helperFn182 &&
          helperFn182["randomFrames"] &&
          (value74 =
            helperFn182["randomFrames"][
              Math["floor"](
                Math["random"]() * helperFn182["randomFrames"]["length"],
              )
            ]),
        value74)
      ) {
        let value75 = value72,
          helperFn22 = flipY(value73);
        const value76 =
          (helperFn182["type"] === portal || helperFn182["type"] === speed) &&
          value74["includes"]("_front_");
        if (value76) {
          const value78 = value74["replace"]("_front_", "_back_");
          let helperFn53 = addImageFromAtlas(
            scene,
            value75,
            helperFn22,
            value78,
          );
          helperFn53 &&
            (this["_applyVisualProps"](scene, helperFn53, value78, item),
            (helperFn53["_eeLayer"] = 1),
            (helperFn53["_eeWorldX"] = value72),
            (helperFn53["_eeBaseY"] = helperFn22),
            this["_addToSection"](helperFn53));
        }
        helperFn182["glow"] &&
          this["_addGlowSprite"](
            scene,
            value75,
            helperFn22,
            value74,
            item,
            value72,
          );
        const value77 = value76
          ? {
              ...helperFn182,
              _portalFront: !0,
            }
          : helperFn182;
        let helperFn52 = addImageFromAtlas(scene, value75, helperFn22, value74);
        if (
          (helperFn52 &&
            (this["_applyVisualProps"](
              scene,
              helperFn52,
              value74,
              item,
              helperFn182,
            ),
            this["_addVisualSprite"](helperFn52, value77),
            (helperFn52["_eeWorldX"] = value72),
            (helperFn52["_eeBaseY"] = helperFn22),
            this["_addToSection"](helperFn52)),
          helperFn182 &&
            (helperFn182["type"] === solid2 || helperFn182["type"] === hazard2))
        ) {
          let value78 = value74["replace"]("_001.png", "_2_001.png"),
            value79 = findAtlasFrame(scene, value78)
              ? addImageFromAtlas(scene, value75, helperFn22, value78)
              : null;
          value79 &&
            (this["_applyVisualProps"](scene, value79, value78, item),
            this["_addVisualSprite"](value79),
            (value79["_eeWorldX"] = value72),
            (value79["_eeBaseY"] = helperFn22),
            this["_addToSection"](value79));
        }
        if (helperFn182["children"])
          for (let item2 of helperFn182["children"]) {
            let value78 = item2["dx"] || 0,
              value79 = item2["dy"] || 0;
            if (void 0 !== item2["localDx"] || void 0 !== item2["localDy"]) {
              let value80 = item2["localDx"] || 0,
                value81 = item2["localDy"] || 0;
              (item["flipX"] && (value80 = -value80),
                item["flipY"] && (value81 = -value81));
              let value82 = ((item["rot"] || 0) * Math["PI"]) / 180;
              ((value78 =
                value80 * Math["cos"](value82) -
                value81 * Math["sin"](value82)),
                (value79 =
                  value80 * Math["sin"](value82) +
                  value81 * Math["cos"](value82)));
            }
            let helperFn53 = addImageFromAtlas(
              scene,
              value75 + value78,
              helperFn22 + value79,
              item2["frame"],
            );
            helperFn53 &&
              (this["_applyVisualProps"](
                scene,
                helperFn53,
                item2["frame"],
                item,
                item2,
              ),
              item2["audioScale"] &&
                (helperFn53["setScale"](0.1),
                helperFn53["setAlpha"](0.9),
                (helperFn53["_eeAudioScale"] = !0),
                this["_audioScaleSprites"]["push"](helperFn53)),
              (void 0 !== item2["z"] ? item2["z"] : -1) < 0
                ? ((helperFn53["_eeLayer"] = 1),
                  (helperFn53["_eeBehindParent"] = !0))
                : this["_addVisualSprite"](helperFn53, item2),
              (helperFn53["_eeWorldX"] = value72 + value78),
              (helperFn53["_eeBaseY"] = helperFn22 + value79),
              this["_addToSection"](helperFn53));
          }
      } else helperFn182 || Set2["add"](item["id"]);
      if (helperFn182 && helperFn182["portalParticle"] && value74) {
        let value75 = value72,
          helperFn22 = flipY(value73);
        const num32 = 2;
        let value76 = value75 - 5 * num32,
          value77 = helperFn22;
        const options10 = {
            getRandomPoint: (point) => {
              let value78 = ((190 * Math["random"]() + 85) * Math["PI"]) / 180,
                value79 = 20 * num32 + 40 * Math["random"]() * num32;
              return (
                (point["x"] = Math["cos"](value78) * value79),
                (point["y"] = Math["sin"](value78) * value79),
                point
              );
            },
          },
          num33 = 20;
        let particles = scene["add"]["particles"](
          value76,
          value77,
          "GJ_WebSheet",
          {
            frame: "square.png",
            lifespan: {
              min: 200,
              max: 1e3,
            },
            speed: 0,
            scale: {
              start: 0.75,
              end: 0.125,
            },
            alpha: {
              start: 0.5,
              end: 0,
            },
            tint: helperFn182["portalParticleColor"],
            blendMode: Phaser["BlendModes"]["ADD"],
            frequency: 20,
            maxParticles: 0,
            emitting: !0,
            emitZone: {
              type: "random",
              source: options10,
            },
            emitCallback: (point) => {
              let value78 = -point["x"],
                value79 = -point["y"],
                value80 =
                  Math["sqrt"](value78 * value78 + value79 * value79) || 1,
                value81 = point["life"] / 1e3,
                value82 = (value80 - num33) / (value81 || 0.3);
              ((point["velocityX"] = (value78 / value80) * value82),
                (point["velocityY"] = (value79 / value80) * value82));
            },
          },
        );
        (particles["setDepth"](14),
          (particles["_eeLayer"] = 2),
          (particles["_eeWorldX"] = value72),
          (particles["_eeBaseY"] = value77),
          this["_addToSection"](particles));
      }
      if (helperFn182)
        if (
          helperFn182["type"] === solid2 &&
          helperFn182["gridW"] > 0 &&
          helperFn182["gridH"] > 0
        ) {
          let value75 = helperFn182["gridW"] * baseUnit,
            value76 = helperFn182["gridH"] * baseUnit,
            LevelObject2 = new LevelObject(
              solid,
              value72,
              value73,
              value75,
              value76,
            );
          (this["objects"]["push"](LevelObject2),
            this["_addCollisionToSection"](LevelObject2));
        } else if (helperFn182["type"] === hazard2) {
          let num32 = 0,
            num33 = 0;
          if (
            (helperFn182["spriteW"] > 0 &&
            helperFn182["spriteH"] > 0 &&
            void 0 !== helperFn182["hitboxScaleX"] &&
            void 0 !== helperFn182["hitboxScaleY"]
              ? ((num32 =
                  helperFn182["spriteW"] * helperFn182["hitboxScaleX"] * 2),
                (num33 =
                  helperFn182["spriteH"] * helperFn182["hitboxScaleY"] * 2))
              : helperFn182["gridW"] > 0 &&
                helperFn182["gridH"] > 0 &&
                ((num32 = 12 * helperFn182["gridW"]),
                (num33 = 24 * helperFn182["gridH"])),
            num32 > 0 && num33 > 0)
          ) {
            let LevelObject2 = new LevelObject(
              hazard,
              value72,
              value73,
              num32,
              num33,
            );
            (this["objects"]["push"](LevelObject2),
              this["_addCollisionToSection"](LevelObject2));
          }
        } else if (helperFn182["type"] === portal) {
          let num32 = 90,
            value75 = helperFn182["gridH"] * baseUnit,
            value76 = null;
          if (
            ("fly" === helperFn182["sub"]
              ? (value76 = portalFly)
              : "cube" === helperFn182["sub"] && (value76 = portalCube),
            value76)
          ) {
            let LevelObject2 = new LevelObject(
              value76,
              value72,
              value73,
              num32,
              value75,
            );
            ((LevelObject2["portalY"] = value73),
              this["objects"]["push"](LevelObject2),
              this["_addCollisionToSection"](LevelObject2));
          }
        }
    }
    (Set2["size"],
      this["_colorTriggers"]["sort"]((itemA, itemB) => itemA["x"] - itemB["x"]),
      this["_enterEffectTriggers"]["sort"](
        (itemA, itemB) => itemA["x"] - itemB["x"],
      ),
      (this["endXPos"] = Math["max"](
        screenWidth + 1200,
        this["_lastObjectX"] + 680,
      )));
  }
  ["createEndPortal"](value71) {
    var value72;
    if (this["endXPos"] <= 0) return;
    const endXPos = this["endXPos"],
      helperFn22 = flipY(240),
      value73 = Math["round"](16);
    this["_endPortalContainer"] = value71["add"]["container"](
      endXPos,
      helperFn22,
    );
    for (let i = 0; i < value73; i++) {
      const image = value71["add"]
        ["image"](
          0,
          (i - Math["floor"](value73 / 2)) * baseUnit,
          "GJ_WebSheet",
          "square_02_001.png",
        )
        ["setAngle"](-90);
      this["_endPortalContainer"]["add"](image);
    }
    (this["container"]["add"](this["_endPortalContainer"]),
      (this["_endPortalShine"] = value71["add"]["image"](
        endXPos - 58,
        helperFn22,
        "GJ_WebSheet",
        "gradientBar.png",
      )));
    const value74 =
      (null ==
      (value72 = value71["textures"]["getFrame"](
        "GJ_WebSheet",
        "gradientBar.png",
      ))
        ? void 0
        : value72["height"]) || 64;
    (this["_endPortalShine"]["setBlendMode"](blendAdd),
      this["_endPortalShine"]["setTint"](colorGreenTint),
      this["_endPortalShine"]["setScale"](1, 960 / value74),
      this["additiveContainer"]["add"](this["_endPortalShine"]));
    const value75 = endXPos - 30,
      options10 = {
        getRandomPoint: (point) => {
          const value76 = ((85 + 190 * Math["random"]()) * Math["PI"]) / 180,
            value77 = 320 + 80 * (2 * Math["random"]() - 1);
          return (
            (point["x"] = Math["cos"](value76) * value77),
            (point["y"] = Math["sin"](value76) * value77),
            point
          );
        },
      };
    ((this["_endPortalEmitter"] = value71["add"]["particles"](
      value75,
      helperFn22,
      "GJ_WebSheet",
      {
        frame: "square.png",
        lifespan: {
          min: 200,
          max: 1e3,
        },
        speed: 0,
        scale: {
          start: 0.75,
          end: 0.125,
        },
        alpha: {
          start: 1,
          end: 0,
        },
        tint: colorGreenTint,
        blendMode: Phaser["BlendModes"]["ADD"],
        frequency: 10,
        maxParticles: 100,
        emitting: !0,
        emitZone: {
          type: "random",
          source: options10,
        },
        emitCallback: (point) => {
          const value76 = -point["x"],
            value77 = -point["y"],
            value78 = Math["sqrt"](value76 * value76 + value77 * value77) || 1,
            value79 = (value78 - 20) / (point["life"] / 1e3 || 0.3);
          ((point["velocityX"] = (value76 / value78) * value79),
            (point["velocityY"] = (value77 / value78) * value79));
        },
      },
    )),
      this["_endPortalEmitter"]["setDepth"](14),
      this["topContainer"]["add"](this["_endPortalEmitter"]),
      (this["_endPortalGameY"] = 240));
  }
  ["updateEndPortalY"](value71, other) {
    if (!this["_endPortalContainer"]) return;
    const value72 = 140 + value71;
    let value73;
    value73 = other ? value72 : Math["max"](240, value72);
    const helperFn22 = flipY(value73);
    ((this["_endPortalContainer"]["y"] = helperFn22),
      (this["_endPortalShine"]["y"] = helperFn22),
      (this["_endPortalEmitter"]["y"] = helperFn22),
      (this["_endPortalGameY"] = value73));
  }
  ["checkColorTriggers"](value71) {
    let table4 = [];
    for (; this["_colorTriggerIdx"] < this["_colorTriggers"]["length"];) {
      let point = this["_colorTriggers"][this["_colorTriggerIdx"]];
      if (!(point["x"] <= value71)) break;
      (table4["push"](point), this["_colorTriggerIdx"]++);
    }
    return table4;
  }
  ["resetColorTriggers"]() {
    this["_colorTriggerIdx"] = 0;
  }
  ["_addToSection"](value71) {
    const value72 = Math["max"](0, Math["floor"](value71["_eeWorldX"] / 400));
    (this["_sections"][value72] || (this["_sections"][value72] = []),
      this["_sections"][value72]["push"](value71));
    const value73 = void 0 !== value71["_eeLayer"] ? value71["_eeLayer"] : 1;
    if (2 === value73) return void this["topContainer"]["add"](value71);
    if (!this["_sectionContainers"][value72]) {
      const options10 = {
        additive: this["_scene"]["add"]["container"](0, 0),
        normal: this["_scene"]["add"]["container"](0, 0),
      };
      (this["additiveContainer"]["add"](options10["additive"]),
        this["container"]["add"](options10["normal"]),
        (this["_sectionContainers"][value72] = options10));
    }
    const value722 = this["_sectionContainers"][value72];
    0 === value73
      ? value722["additive"]["add"](value71)
      : value71["_eeBehindParent"]
        ? value722["normal"]["addAt"](value71, 0)
        : value722["normal"]["add"](value71);
  }
  ["_addCollisionToSection"](point) {
    const value71 = Math["max"](0, Math["floor"](point["x"] / 400));
    (this["_collisionSections"][value71] ||
      (this["_collisionSections"][value71] = []),
      this["_collisionSections"][value71]["push"](point));
  }
  ["_setSectionVisible"](value71, other) {
    const value712 = this["_sectionContainers"][value71];
    value712 &&
      ((value712["additive"]["visible"] = other),
      (value712["normal"]["visible"] = other));
  }
  ["updateVisibility"](value71) {
    const value72 = this["_sectionContainers"]["length"] - 1;
    if (value72 < 0) return;
    const value73 = Math["max"](0, Math["floor"]((value71 - 140) / 400)),
      value74 = Math["min"](
        value72,
        Math["floor"]((value71 + screenWidth + 140) / 400),
      ),
      visMinSec = this["_visMinSec"],
      visMaxSec = this["_visMaxSec"];
    if (visMinSec < 0) {
      for (let i = 0; i <= value72; i++)
        this["_setSectionVisible"](i, i >= value73 && i <= value74);
      return (
        (this["_visMinSec"] = value73),
        void (this["_visMaxSec"] = value74)
      );
    }
    if (value73 !== visMinSec || value74 !== visMaxSec) {
      if (value73 > visMinSec)
        for (let i = visMinSec; i <= Math["min"](value73 - 1, visMaxSec); i++)
          this["_setSectionVisible"](i, !1);
      if (value74 < visMaxSec)
        for (let i = Math["max"](value74 + 1, visMinSec); i <= visMaxSec; i++)
          this["_setSectionVisible"](i, !1);
      if (value73 < visMinSec)
        for (let i = value73; i <= Math["min"](visMinSec - 1, value74); i++)
          this["_setSectionVisible"](i, !0);
      if (value74 > visMaxSec)
        for (let i = Math["max"](visMaxSec + 1, value73); i <= value74; i++)
          this["_setSectionVisible"](i, !0);
      ((this["_visMinSec"] = value73), (this["_visMaxSec"] = value74));
    }
  }
  ["getNearbySectionObjects"](value71) {
    const value72 = Math["max"](0, Math["floor"](value71 / 400)),
      value73 = Math["max"](0, value72 - 1),
      value74 = Math["min"](
        this["_collisionSections"]["length"] - 1,
        value72 + 1,
      ),
      nearbyBuffer = this["_nearbyBuffer"];
    nearbyBuffer["length"] = 0;
    for (let i = value73; i <= value74; i++) {
      const i2 = this["_collisionSections"][i];
      if (i2)
        for (let i3 = 0; i3 < i2["length"]; i3++) nearbyBuffer["push"](i2[i3]);
    }
    return nearbyBuffer;
  }
  ["checkEnterEffectTriggers"](value71) {
    for (
      ;
      this["_enterEffectTriggerIdx"] < this["_enterEffectTriggers"]["length"];
    ) {
      let point = this["_enterEffectTriggers"][this["_enterEffectTriggerIdx"]];
      if (!(point["x"] <= value71)) break;
      ((this["_activeEnterEffect"] = point["effect"]),
        (this["_activeExitEffect"] = point["effect"]),
        this["_enterEffectTriggerIdx"]++);
    }
  }
  ["resetEnterEffectTriggers"]() {
    ((this["_enterEffectTriggerIdx"] = 0),
      (this["_activeEnterEffect"] = 0),
      (this["_activeExitEffect"] = 0));
    for (let i = 0; i < this["_sections"]["length"]; i++) {
      this["_setSectionVisible"](i, !0);
      const i2 = this["_sections"][i];
      if (i2)
        for (let i3 = 0; i3 < i2["length"]; i3++) {
          const point = i2[i3];
          ((point["_eeActive"] = !1),
            (point["visible"] = !0),
            (point["x"] = point["_eeWorldX"]),
            (point["y"] = point["_eeBaseY"]),
            point["_eeAudioScale"] || point["setScale"](1),
            point["setAlpha"](1));
        }
    }
  }
  ["applyEnterEffects"](value71) {
    const num32 = 400,
      num33 = 140,
      num34 = 200,
      value72 = value71,
      value73 = value71 + screenWidth,
      value74 = value71 + screenWidth / 2,
      value75 = Math["max"](0, Math["floor"]((value72 - num33) / num32)),
      value76 = Math["min"](
        this["_sections"]["length"] - 1,
        Math["floor"]((value73 + num33) / num32),
      );
    for (let i = value75; i <= value76; i++) {
      const i2 = this["_sections"][i];
      if (!i2) continue;
      const value77 = i * num32,
        value78 =
          value77 >= value72 + num33 && value77 + num32 <= value73 - num33;
      for (let i3 = 0; i3 < i2["length"]; i3++) {
        const point = i2[i3];
        if (value78) {
          point["_eeActive"] &&
            ((point["_eeActive"] = !1),
            (point["y"] = point["_eeBaseY"]),
            (point["x"] = point["_eeWorldX"]),
            point["_eeAudioScale"] || point["setScale"](1),
            point["setAlpha"](1));
          continue;
        }
        const eeWorldX = point["_eeWorldX"],
          value79 = eeWorldX > value74;
        let value80;
        if (
          ((value80 = value79
            ? Math["max"](0, Math["min"](1, (value73 - eeWorldX) / num33))
            : Math["max"](0, Math["min"](1, (eeWorldX - value72) / num33))),
          value80 >= 1)
        ) {
          point["_eeActive"] &&
            ((point["_eeActive"] = !1),
            (point["y"] = point["_eeBaseY"]),
            (point["x"] = point["_eeWorldX"]),
            point["_eeAudioScale"] || point["setScale"](1),
            point["setAlpha"](1));
          continue;
        }
        point["_eeActive"] = !0;
        const value81 = value79
            ? this["_activeEnterEffect"]
            : this["_activeExitEffect"],
          value82 = 1 - value80;
        let eeBaseY = point["_eeBaseY"],
          eeWorldX2 = point["_eeWorldX"],
          value83 = value80,
          num35 = 1;
        switch (value81) {
          case 0:
            break;
          case 1:
            eeBaseY = point["_eeBaseY"] + num34 * value82;
            break;
          case 2:
            eeBaseY = point["_eeBaseY"] - num34 * value82;
            break;
          case 3:
            eeWorldX2 = point["_eeWorldX"] - num34 * value82;
            break;
          case 4:
            eeWorldX2 = point["_eeWorldX"] + num34 * value82;
            break;
          case 5:
            point["_eeAudioScale"] || (num35 = value80);
            break;
          case 6:
            point["_eeAudioScale"] || (num35 = 1 + 0.75 * value82);
        }
        (point["x"] !== eeWorldX2 && (point["x"] = eeWorldX2),
          point["y"] !== eeBaseY && (point["y"] = eeBaseY),
          point["alpha"] !== value83 && (point["alpha"] = value83),
          point["_eeAudioScale"] ||
            point["scaleX"] === num35 ||
            point["setScale"](num35));
      }
    }
  }
  ["setGroundColor"](value71) {
    for (let item of this["_groundTiles"]) item["setTint"](value71);
    for (let item of this["_ceilingTiles"]) item["setTint"](value71);
  }
  ["updateAudioScale"](value71) {
    for (let item of this["_audioScaleSprites"]) item["setScale"](value71);
  }
  ["resetVisibility"]() {
    ((this["_visMinSec"] = -1), (this["_visMaxSec"] = -1));
  }
  ["resetObjects"]() {
    for (let item of this["objects"]) item["activated"] = !1;
    for (let item of this["_audioScaleSprites"]) item["setScale"](0.1);
  }
}
