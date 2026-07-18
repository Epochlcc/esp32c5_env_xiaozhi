# DISPLAY: LVGL/LCD/OLED Rendering Pipeline

Multi-layered display stack supporting LCD (SPI/QSPI), OLED (I2C), and LVGL-driven screens. Manages emoji playback, environment data panels, and chat UI.

## STACK ARCHITECTURE

```
Application Layer (chat UI, env data)
  └── LVGL Display (lvgl_display/)
      ├── lvgl_display.{h,cc}       # LVGL init, screen management, touch binding
      ├── lvgl_theme.{h,cc}         # Theme colors, fonts, styles
      ├── lvgl_font.{h,cc}          # Font loading (Chinese/English/Japanese)
      ├── lvgl_image.{h,cc}         # Image asset loading (PNG/JPG spiffs)
      ├── emoji_collection.{h,cc}   # Emoji animation state machine
      ├── gif/                       # GIF decoder (gifdec + LVGL wrapper)
      └── jpg/                       # JPEG encode/decode utilities
  ├── lcd_display.{h,cc}           # LCD panel wrapper (ILI9341/ST77916/GC9A01...)
  ├── oled_display.{h,cc}          # SSD1306/SH1106 I2C OLED
  ├── emote_display.{h,cc}         # Espressive Emotions (face animation)
  └── display.{h,cc}               # Polymorphic display factory
```

## KEY FILES

| File | Purpose |
|------|---------|
| `lvgl_display/lvgl_display.cc` | LVGL port init, input device binding, screen load |
| `lvgl_display/emoji_collection.cc` | Emoji animation sequences (talk/listen/think/sleep) |
| `lvgl_display/lvgl_theme.cc` | Color scheme — auto-switch dark/light by time |
| `lcd_display.cc` | SPI LCD driver selection + panel orientation |
| `oled_display.cc` | I2C OLED 128×64 — frugal rendering for C3 boards |

## CONVENTIONS

- **LVGL v8.3+** via `espressif/esp_lvgl_port`
- **Spiffs assets**: emoji GIFs, background images, fonts stored in SPIFFS partition
- **Board config**: display type selected by `board->GetDisplay()`
- **Orientation**: rotation + mirror set per board in BSP `config.h`

## ANTI-PATTERNS

- **DO NOT** draw directly to LCD framebuffer — always go through LVGL or display HAL
- **NEVER** load large images (>100KB) without LVGL image cache management
- **DO NOT** refresh full screen — use LVGL invalidation zones
