// Picker - Static utility class for runtime screen-ray picking (VK-1301).
//
// Converts a screen-space pixel (use Input::getViewportMouseX/Y) into world-space
// picks against the terrain heightfield and scene entities, using the primary
// camera + active viewport resolved internally on the engine side.
//
// Usage:
//   float mx = Input::getViewportMouseX();
//   float my = Input::getViewportMouseY();
//
//   RaycastHit t = Picker::pickTerrain(mx, my);
//   if (t.hit) { Log::info("terrain @ " + t.point.x + "," + t.point.y + "," + t.point.z); }
//
//   RaycastHit e = Picker::pickEntity(mx, my, "Dynamic");
//   if (e.hit) { Log::info("entity " + e.entityId); }

import * from "../math/Vec3f.mt";
import * from "RaycastHit.mt";
import * from "ScreenPoint.mt";

public class Picker {
    public constructor() {
    }

    // World-space terrain point under the screen pixel.
    // RaycastHit.hit is false if the ray misses the loaded terrain.
    public static function pickTerrain(float screenX, float screenY): RaycastHit {
        float[] raw = _native_picker_pickTerrainPoint(screenX, screenY);
        if (raw[0] < 0.5) {
            return new RaycastHit();
        }
        RaycastHit h = new RaycastHit();
        h.hit = true;
        h.point = new Vec3f(raw[1], raw[2], raw[3]);
        return h;
    }

    // Terrain pick via a Jolt physics raycast against the "Static" layer (the terrain
    // heightfield collider). Returns hit entity + point + surface normal. Requires the
    // terrain to have a physics collider; use pickTerrain for a collider-free pick.
    public static function pickTerrainPhysics(float screenX, float screenY): RaycastHit {
        return Picker::pickEntity(screenX, screenY, "Static");
    }

    // Physics terrain pick filtered by comma-separated layer names
    // (e.g. "Static" or "Static,Dynamic").
    public static function pickTerrainPhysics(float screenX, float screenY, string layerMask): RaycastHit {
        return Picker::pickEntity(screenX, screenY, layerMask);
    }

    // First entity under the screen pixel (physics raycast, all layers).
    public static function pickEntity(float screenX, float screenY): RaycastHit {
        float[] raw = _native_picker_pickEntity(screenX, screenY, "");
        if (raw[0] < 0.5) {
            return new RaycastHit();
        }
        return new RaycastHit(
            true,
            (int)raw[1],
            new Vec3f(raw[2], raw[3], raw[4]),
            new Vec3f(raw[5], raw[6], raw[7]),
            raw[8]
        );
    }

    // First entity under the screen pixel, filtered by comma-separated layer names
    // (e.g. "Dynamic" or "Dynamic,Kinematic").
    public static function pickEntity(float screenX, float screenY, string layerMask): RaycastHit {
        float[] raw = _native_picker_pickEntity(screenX, screenY, layerMask);
        if (raw[0] < 0.5) {
            return new RaycastHit();
        }
        return new RaycastHit(
            true,
            (int)raw[1],
            new Vec3f(raw[2], raw[3], raw[4]),
            new Vec3f(raw[5], raw[6], raw[7]),
            raw[8]
        );
    }

    // World position -> viewport pixel (inverse of the screen ray; same pixel space
    // as Input::getViewportMouseX/Y). ScreenPoint.visible is false when there is no
    // primary camera or the point is behind the camera; x/y may lie outside the
    // viewport for in-front-but-offscreen points.
    public static function worldToScreen(float worldX, float worldY, float worldZ): ScreenPoint {
        float[] raw = _native_picker_worldToScreen(worldX, worldY, worldZ);
        if (raw[0] < 0.5) {
            return new ScreenPoint();
        }
        return new ScreenPoint(true, raw[1], raw[2]);
    }
}
