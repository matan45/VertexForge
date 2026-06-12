# Runtime UI Expansion

New runtime UI capabilities added by the UI expansion epic: themes, rich text,
tooltips, modal windows, and prefab-driven list views. All features follow the
standard component pattern (inspector drawer, scene/prefab serialization,
`UI::` script natives, listener interfaces).

## Theme / Style System (`.vfTheme`)

A theme is a JSON asset holding named **styles**; each style is a property bag
of colors, floats, and asset refs. Widgets opt in with a `UIStyle` component
(`styleKey`); the canvas references the theme (`UICanvasComponent.themeRef`).

Applying a theme **writes concrete values into the existing component fields**
(no per-frame lookups): re-applied on scene load, on style-key change, and on
demand. Saved scenes bake the themed values; the styleKey remains the source
of truth on next apply.

Recognized property names (`UIThemeApplier`):

| Kind   | Names |
|--------|-------|
| colors | `labelColor`, `imageTint`, `buttonNormalColor`, `buttonHoveredColor`, `buttonPressedColor`, `buttonDisabledColor`, `progressTrackColor`, `progressFillColor` |
| floats | `fontSize` |
| assets | `font`, `imageTexture`, `buttonNormalTexture`, `buttonHoveredTexture`, `buttonPressedTexture`, `buttonDisabledTexture` |

Authoring: content browser **Create > UI Theme**, then **Tools > UI Theme
Editor** (open/edit/save + "Apply to Scene"). Assign per canvas in the UI
Canvas drawer.

Scripts: `UI::setCanvasTheme(canvasId, path)`, `UI::setStyleKey(id, key)`,
`UI::reapplyTheme(canvasId /* -1 = all */)`.

`.vfTheme` format:

```json
{
  "version": 1,
  "styles": {
    "Heading":      { "colors": { "labelColor": [1, 0.8, 0.2, 1] }, "floats": { "fontSize": 28 } },
    "ActionButton": { "colors": { "buttonNormalColor": [0.1, 0.3, 0.6, 1] } }
  }
}
```

## Rich Text (UILabel)

Opt-in per label (`richText` checkbox / `UI::setLabelRichText`). Markup is a
BBCode subset parsed before layout; styles resolve per glyph through the
existing SDF text pipeline (no shader changes).

- `[b]bold[/b]`, `[i]italic[/i]`, `[color=#RRGGBB]...[/color]` (also `#RRGGBBAA`)
- Tags nest; `[[` escapes a literal `[`; unknown/malformed tags render literally
- `[icon=...]` is reserved (consumed, renders nothing) for a future inline-icon pass
- Span colors inherit the label's alpha (fade animations still work)
- Known limits: bold is an SDF dilate so bold runs measure at normal advance;
  world-space (edit mode) labels render the *stripped* text without styles

## Tooltip (UITooltipComponent)

Hover-delay tooltip on any UI element with a rect. Two modes:

- **Text** — the engine draws a synthetic bubble (background + wrapped text)
  topmost on the UI overlay layer; follows the cursor or anchors to the
  element; placement clamps to the viewport and flips near the bottom edge.
  Needs a font ref for the text. Bubble size uses the avg-char-width estimate
  (same as the text-input caret).
- **ChildPanel** — a designated (inactive) child entity is shown while
  hovered (`panelChildName`, or the first inactive child panel). The child
  should set `blocksRaycast = false`.

Scripts: `UI::setTooltipText/getTooltipText/setTooltipEnabled/setTooltipDelay`.
Tooltips only fire in play mode, and never for elements under an active modal.

## Modal Window (UIWindowComponent)

A window panel with optional title bar (drag to move), close button, and
**modal** mode: a dim backdrop that blocks every pointer interaction outside
the window subtree (buttons, sliders, scroll, drag, tooltips, and world
clicks — pointer-over-UI is forced on).

- Window visibility = the entity's active state. `UI::openWindow` /
  `UI::closeWindow` toggle it, maintain the modal stack, and fire
  `IUIWindowListener.onWindowOpened/onWindowClosed`.
- Stacked modals: the top of the stack wins; closing it restores the one below.
- Window content = regular UI children of the window entity (anchored to its
  rect). The whole subtree renders on the UI overlay layer, above the backdrop.

## UI Overlay Render Layer

`UIImageRenderData/UITextRenderData.overlay` routes draws into a second
graph-managed pass that records **after all main UI images and text**, so
overlay backgrounds (tooltips, modal backdrops, window chrome) cover
underlying labels too. Order within the overlay: backdrop → window chrome →
window content → tooltip.

## Prefab Asset Type + UIListView

`.vfPrefab` files are now first-class assets (`AssetType::Prefab`): they get
`.vfmeta` sidecars + GUIDs on save / project scan and can be referenced by
`AssetRef` like any other asset.

`UIListViewComponent` binds an **item count** to a `.vfPrefab` **item
template**:

- The engine instantiates/pools/destroys item instances under the list entity
  (`UIListItemComponent` marker links each instance to its list + index).
- A `UILayoutGroupComponent` on the list entity (auto-added) positions items
  via the existing per-frame layout pass; add a `UIScroll` for long lists.
- Item instances are **never serialized** — scenes/prefabs skip marked
  children and rebuild instances on load.
- Shrinking deactivates surplus instances into a pool; growth reuses the pool
  before instantiating.
- Click-to-select (optional): selected item gets a tint overlay and fires
  `IUIListViewListener.onListSelectionChanged(listId, name, prev, new)`.

Scripts:

```mt
UI::setListItemTemplate(listId, "ui/QueueSlot.vfPrefab");
UI::setListItemCount(listId, queue.size());
for (int i = 0; i < queue.size(); i = i + 1) {
    int item = UI::getListItem(listId, i);
    // walk the item's children (Entity::getChildren + Entity::getName)
    // to find the parts authored in the template, then fill them in:
    int[] parts = Entity::getChildren(item);
    for (int p = 0; p < parts.length; p = p + 1) {
        if (Entity::getName(parts[p]) == "Icon") {
            UI::setImageTexture(parts[p], queue.get(i).iconPath);
        }
    }
}
```
