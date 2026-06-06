// ScreenPoint - Holds the result of a world-to-screen projection (VK-1302)
//
// Usage:
//   ScreenPoint p = Picker::worldToScreen(pos.x, pos.y, pos.z);
//   if (p.visible) {
//       Log::info("on screen at " + p.x + "," + p.y);
//   }
//
// visible is false when there is no primary camera or the point is behind the
// camera. x/y may lie outside the viewport for in-front-but-offscreen points.

public class ScreenPoint {
    public bool visible = false;
    public float x = 0.0;
    public float y = 0.0;

    public constructor() {
    }

    public constructor(bool visible, float x, float y) {
        this.visible = visible;
        this.x = x;
        this.y = y;
    }
}
