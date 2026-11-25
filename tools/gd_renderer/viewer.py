"""
Geometry Dash Interactive Editor Viewer
Interactive Pygame-based viewport for Geometry Dash levels.
Supports pan, zoom, grid, object inspection, level switching from CCLocalLevels.dat or files,
and high-resolution image export.
"""

import os
import math
import time
import tkinter as tk
from tkinter import filedialog, simpledialog
from typing import Optional, Tuple

import pygame

from .asset_manager import AssetManager
from .level_parser import GDLevel, LevelObject, LevelParser
from .renderer import GDRenderer
from .geometry import object_bounds

class GDViewer:
    """Interactive Geometry Dash level editor viewer."""

    def __init__(self, initial_level: Optional[GDLevel] = None, quality: str = "uhd",
                 resources_dir: Optional[str] = None, antialias: int = 2, preview_x=None):
        pygame.init()
        pygame.font.init()
        
        self.width = 1280
        self.height = 720
        self.screen = pygame.display.set_mode((self.width, self.height), pygame.RESIZABLE)
        pygame.display.set_caption("Geometry Dash Level Viewer - Precise Editor Render")
        
        self.clock = pygame.time.Clock()
        self.running = True
        
        # Load assets & renderer
        self.quality = quality
        self.asset_manager = AssetManager(resources_dir=resources_dir, quality=quality)
        self.renderer = GDRenderer(asset_manager=self.asset_manager, quality=quality)
        self.object_defs = LevelParser.get_object_defs()
        
        self.antialias = antialias
        self.preview_x = preview_x
        self._frame_key = None
        self._frame_surface = None
        self._visible_count = 0
        
        # Fonts
        self.font = pygame.font.SysFont("Segoe UI, Arial, sans-serif", 16)
        self.font_bold = pygame.font.SysFont("Segoe UI, Arial, sans-serif", 18, bold=True)
        self.font_small = pygame.font.SysFont("Segoe UI, Arial, sans-serif", 13)
        
        # Viewer settings & state
        self.draw_grid = True
        self.draw_bg = True
        self.draw_ground = True
        self.draw_glow = True
        self.show_hud = True
        
        # Camera
        # cam_x, cam_y is the world coordinate at center of screen
        self.cam_x: float = 0.0
        self.cam_y: float = 300.0
        self.zoom: float = 1.0  # Screen pixels per GD world unit (1 block = 30 * zoom px)
        self.min_zoom: float = 0.05
        self.max_zoom: float = 8.0
        
        # Mouse drag state
        self.is_dragging = False
        self.drag_start_mouse = (0, 0)
        self.drag_start_cam = (0.0, 0.0)
        
        # Current level
        self.level: GDLevel = initial_level or GDLevel("Empty Level")
            
        # Notification popup message
        self.notification_msg = ""
        self.notification_time = 0.0
        
        # Inspected object
        self.selected_object: Optional[LevelObject] = None
        if initial_level:
            self.set_level(initial_level)

    def show_notification(self, text: str, duration: float = 3.5):
        self.notification_msg = text
        self.notification_time = time.time() + duration

    def set_level(self, level: GDLevel):
        """Loads and switches to a new level."""
        self.level = level
        self.renderer.tinted_cache.clear()
        self._frame_key = None
        self.selected_object = None
        
        # Center camera on level
        bounds = level.get_bounds()
        if level.objects:
            self.cam_x = bounds[0] + 400.0
            self.cam_y = max(200.0, (bounds[1] + bounds[3]) / 2.0)
            self.zoom = 1.0
        else:
            self.cam_x = 0.0
            self.cam_y = 300.0
            self.zoom = 1.0
            
        pygame.display.set_caption(f"GD Viewer - {level.name} ({level.object_count} objects)")
        self.show_notification(f"Loaded level: {level.name} ({level.object_count} objects)")

    def world_to_screen(self, wx: float, wy: float) -> Tuple[int, int]:
        """Converts GD world coordinates to screen pixel coordinates."""
        sx = (wx - self.cam_x) * self.zoom + self.width / 2.0
        sy = (self.cam_y - wy) * self.zoom + self.height / 2.0
        return int(round(sx)), int(round(sy))

    def screen_to_world(self, sx: int, sy: int) -> Tuple[float, float]:
        """Converts screen pixel coordinates to GD world coordinates."""
        wx = (sx - self.width / 2.0) / self.zoom + self.cam_x
        wy = self.cam_y - (sy - self.height / 2.0) / self.zoom
        return wx, wy

    def draw(self):
        """Render with the PNG engine and reuse the frame while the camera is idle."""
        w_left, w_top = self.screen_to_world(0, 0)
        w_right, w_bottom = self.screen_to_world(self.width, self.height)
        key = (id(self.level), self.cam_x, self.cam_y, self.zoom, self.width, self.height,
               self.draw_bg, self.draw_ground, self.draw_grid, self.draw_glow,
               self.antialias, self.preview_x)
        if key != self._frame_key:
            image = self.renderer.render_range(
                self.level, w_left, w_right, w_bottom, w_top, scale=self.zoom,
                draw_bg=self.draw_bg, draw_ground=self.draw_ground,
                draw_grid=self.draw_grid, draw_glow=self.draw_glow,
                antialias=self.antialias, preview_x=self.preview_x)
            self._frame_surface = pygame.image.frombytes(image.tobytes(), image.size, image.mode)
            self._visible_count = len(self.renderer.visible_objects(
                self.level, w_left, w_right, w_bottom, w_top))
            self._frame_key = key
        self.screen.fill((20, 20, 24))
        self.screen.blit(self._frame_surface, (0, 0))

        if self.selected_object:
            bounds = object_bounds(self.selected_object, self.object_defs.get(str(self.selected_object.id), {}))
            left, top = self.world_to_screen(bounds[0], bounds[3])
            right, bottom = self.world_to_screen(bounds[2], bounds[1])
            pygame.draw.rect(self.screen, (0, 255, 255),
                             (left, top, max(1, right - left), max(1, bottom - top)), 1)
        if self.show_hud:
            self._draw_hud(w_left, w_right, w_top, w_bottom, self._visible_count)
        if time.time() < self.notification_time and self.notification_msg:
            self._draw_notification()
        pygame.display.flip()

    def _draw_hud(self, w_left: float, w_right: float, w_top: float, w_bottom: float, vis_count: int):
        """Draws informational HUD header and bottom status bar."""
        # Top banner
        header_surf = pygame.Surface((self.width, 36), pygame.SRCALPHA)
        header_surf.fill((15, 15, 20, 220))
        self.screen.blit(header_surf, (0, 0))
        
        # Level title & stats
        title_text = f"{self.level.name}"
        title_render = self.font_bold.render(title_text, True, (255, 255, 255))
        self.screen.blit(title_render, (15, 7))
        
        stats_text = f"Objects: {vis_count} visible / {self.level.object_count} total | Zoom: {self.zoom * 100:.0f}% | Cam: ({self.cam_x:.1f}, {self.cam_y:.1f})"
        stats_render = self.font.render(stats_text, True, (200, 200, 210))
        self.screen.blit(stats_render, (title_render.get_width() + 30, 9))
        
        # Bottom controls bar
        bot_surf = pygame.Surface((self.width, 28), pygame.SRCALPHA)
        bot_surf.fill((15, 15, 20, 220))
        self.screen.blit(bot_surf, (0, self.height - 28))
        
        mx, my = pygame.mouse.get_pos()
        cursor_wx, cursor_wy = self.screen_to_world(mx, my)
        cursor_text = f"Cursor: ({cursor_wx:.1f}, {cursor_wy:.1f}) [Block: {cursor_wx/30:.1f}, {cursor_wy/30:.1f}]"
        
        hints = "[Drag Right/Mid]: Pan | [Wheel]: Zoom | [O]: Open File | [M]: Level List | [S]: Save PNG | [G]: Grid | [B]: BG | [F]: Floor | [L]: Glow"
        self.screen.blit(self.font_small.render(cursor_text, True, (0, 255, 200)), (15, self.height - 22))
        self.screen.blit(self.font_small.render(hints, True, (170, 170, 180)), (320, self.height - 22))

        # Selected object inspection card
        if self.selected_object:
            self._draw_object_card(self.selected_object)

    def _draw_object_card(self, obj: LevelObject):
        """Draws inspection panel for selected object."""
        card_w = 260
        card_h = 180
        card_x = self.width - card_w - 15
        card_y = 50
        
        panel = pygame.Surface((card_w, card_h), pygame.SRCALPHA)
        panel.fill((20, 25, 35, 240))
        pygame.draw.rect(panel, (0, 200, 255), (0, 0, card_w, card_h), 1)
        self.screen.blit(panel, (card_x, card_y))
        
        title = self.font_bold.render(f"Object ID: {obj.id}", True, (0, 255, 255))
        self.screen.blit(title, (card_x + 12, card_y + 10))
        
        lines = [
            f"Position: ({obj.x:.1f}, {obj.y:.1f})",
            f"Scale: ({obj.scale * obj.scale_x:.2f}, {obj.scale * obj.scale_y:.2f})",
            f"Rotation: {obj.rotation:.1f} deg",
            f"Rotation X/Y: {obj.rotation_x if obj.rotation_x is not None else obj.rotation:.1f} / {obj.rotation_y if obj.rotation_y is not None else obj.rotation:.1f}",
            f"Z-Layer: {obj.z_layer} | Z-Order: {obj.z_order}",
            f"Color 1 (Base): {obj.base_color_channel}",
            f"Color 2 (Detail): {obj.detail_color_channel}",
        ]
        
        for i, line in enumerate(lines):
            t = self.font_small.render(line, True, (220, 220, 220))
            self.screen.blit(t, (card_x + 12, card_y + 35 + i * 19))

    def _draw_notification(self):
        """Draws temporary notification pill in center top."""
        text_surf = self.font_bold.render(self.notification_msg, True, (255, 255, 255))
        padding = 16
        pill_w = text_surf.get_width() + padding * 2
        pill_h = text_surf.get_height() + 12
        pill_x = (self.width - pill_w) // 2
        pill_y = 48
        
        pill = pygame.Surface((pill_w, pill_h), pygame.SRCALPHA)
        pill.fill((30, 140, 240, 240))
        pygame.draw.rect(pill, (255, 255, 255), (0, 0, pill_w, pill_h), 1, border_radius=6)
        self.screen.blit(pill, (pill_x, pill_y))
        self.screen.blit(text_surf, (pill_x + padding, pill_y + 6))

    def handle_open_file(self):
        """Opens a file dialog to choose a level file."""
        root = tk.Tk()
        root.withdraw()
        root.attributes("-topmost", True)
        
        path = filedialog.askopenfilename(
            title="Open Geometry Dash Level File",
            filetypes=[
                ("All GD Level Files", "*.gmd;*.txt;*.level;*.dat"),
                ("GDShare / Geode (*.gmd)", "*.gmd"),
                ("Text / Raw level (*.txt;*.level)", "*.txt;*.level"),
                ("CCLocalLevels (*.dat)", "*.dat"),
                ("All files", "*.*")
            ]
        )
        root.destroy()
        
        if path:
            try:
                new_lvl = LevelParser.parse_file(path)
                self.set_level(new_lvl)
            except Exception as e:
                self.show_notification(f"Error loading file: {e}")

    def handle_choose_local_level(self):
        """Shows dialog to pick from local CCLocalLevels.dat levels."""
        levels = LevelParser.list_local_levels()
        if not levels:
            self.show_notification("No local levels found in CCLocalLevels.dat.")
            return
            
        root = tk.Tk()
        root.withdraw()
        root.attributes("-topmost", True)
        
        # Build prompt listing recent 15 levels
        prompt_lines = ["Enter Level Name or Number (0 to 14):"]
        for i in range(min(15, len(levels))):
            prompt_lines.append(f"{i}: {levels[i]['name']}")
            
        choice = simpledialog.askstring("Select Local GD Level", "\n".join(prompt_lines))
        root.destroy()
        
        if choice is not None and choice.strip():
            val = choice.strip()
            try:
                idx = int(val)
                lvl = LevelParser.load_local_level(idx)
            except ValueError:
                lvl = LevelParser.load_local_level(val)
                
            if lvl:
                self.set_level(lvl)
            else:
                self.show_notification(f"Level '{val}' not found.")

    def export_screenshot(self, full_bounds: bool = False):
        """Exports high-resolution render."""
        timestamp = int(time.time())
        clean_name = "".join(c for c in self.level.name if c.isalnum() or c in (" ", "_", "-")).strip()
        filename = f"{clean_name}_{timestamp}.png"
        
        self.show_notification("Exporting PNG render...")
        pygame.display.flip()
        
        if full_bounds:
            b = self.level.get_bounds()
            img = self.renderer.render_range(
                self.level,
                x_start=b[0] - 60,
                x_end=b[2] + 60,
                y_bottom=min(0.0, b[1] - 60),
                y_top=b[3] + 60,
                scale=1.0,
                draw_bg=self.draw_bg,
                draw_ground=self.draw_ground,
                draw_grid=self.draw_grid,
                draw_glow=self.draw_glow,
                antialias=self.antialias,
                preview_x=self.preview_x
            )
        else:
            w_left, w_top = self.screen_to_world(0, 0)
            w_right, w_bottom = self.screen_to_world(self.width, self.height)
            img = self.renderer.render_range(
                self.level,
                x_start=w_left,
                x_end=w_right,
                y_bottom=w_bottom,
                y_top=w_top,
                scale=self.zoom,
                draw_bg=self.draw_bg,
                draw_ground=self.draw_ground,
                draw_grid=self.draw_grid,
                draw_glow=self.draw_glow,
                antialias=self.antialias,
                preview_x=self.preview_x
            )
            
        img.save(filename)
        self.show_notification(f"Saved render to {filename} ({img.width}x{img.height})")

    def run(self):
        """Main viewer loop."""
        while self.running:
            dt = self.clock.tick(60) / 1000.0
            
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    self.running = False
                    break
                    
                elif event.type == pygame.VIDEORESIZE:
                    self.width = max(400, event.w)
                    self.height = max(300, event.h)
                    self.screen = pygame.display.set_mode((self.width, self.height), pygame.RESIZABLE)
                    
                elif event.type == pygame.MOUSEBUTTONDOWN:
                    if event.button in (2, 3):  # Middle or Right click: start drag
                        self.is_dragging = True
                        self.drag_start_mouse = event.pos
                        self.drag_start_cam = (self.cam_x, self.cam_y)
                    elif event.button == 1:  # Left click: select / inspect object
                        wx, wy = self.screen_to_world(event.pos[0], event.pos[1])
                        # Find closest object within 20 units
                        closest = None
                        min_dist = 25.0
                        for obj in self.level.objects:
                            dist = math.hypot(obj.x - wx, obj.y - wy)
                            if dist < min_dist:
                                min_dist = dist
                                closest = obj
                        self.selected_object = closest
                    elif event.button == 4:  # Wheel up: zoom in centered on mouse
                        old_wx, old_wy = self.screen_to_world(event.pos[0], event.pos[1])
                        self.zoom = min(self.max_zoom, self.zoom * 1.15)
                        # Adjust camera so world point stays under cursor
                        self.cam_x = old_wx - (event.pos[0] - self.width / 2.0) / self.zoom
                        self.cam_y = old_wy + (event.pos[1] - self.height / 2.0) / self.zoom
                    elif event.button == 5:  # Wheel down: zoom out centered on mouse
                        old_wx, old_wy = self.screen_to_world(event.pos[0], event.pos[1])
                        self.zoom = max(self.min_zoom, self.zoom / 1.15)
                        self.cam_x = old_wx - (event.pos[0] - self.width / 2.0) / self.zoom
                        self.cam_y = old_wy + (event.pos[1] - self.height / 2.0) / self.zoom
                        
                elif event.type == pygame.MOUSEBUTTONUP:
                    if event.button in (2, 3):
                        self.is_dragging = False
                        
                elif event.type == pygame.MOUSEMOTION:
                    if self.is_dragging:
                        dx = event.pos[0] - self.drag_start_mouse[0]
                        dy = event.pos[1] - self.drag_start_mouse[1]
                        self.cam_x = self.drag_start_cam[0] - dx / self.zoom
                        self.cam_y = self.drag_start_cam[1] + dy / self.zoom
                        
                elif event.type == pygame.KEYDOWN:
                    mods = pygame.key.get_mods()
                    if event.key == pygame.K_g:
                        self.draw_grid = not self.draw_grid
                    elif event.key == pygame.K_b:
                        self.draw_bg = not self.draw_bg
                    elif event.key == pygame.K_f:
                        self.draw_ground = not self.draw_ground
                    elif event.key == pygame.K_l:
                        self.draw_glow = not self.draw_glow
                    elif event.key == pygame.K_h:
                        self.show_hud = not self.show_hud
                    elif event.key == pygame.K_r:
                        bounds = self.level.get_bounds()
                        self.cam_x = bounds[0] + 400.0
                        self.cam_y = max(200.0, (bounds[1] + bounds[3]) / 2.0)
                        self.zoom = 1.0
                    elif event.key == pygame.K_o:
                        self.handle_open_file()
                    elif event.key == pygame.K_m:
                        self.handle_choose_local_level()
                    elif event.key in (pygame.K_s, pygame.K_F12):
                        full = bool(mods & pygame.KMOD_SHIFT)
                        self.export_screenshot(full_bounds=full)
                    elif event.key in (pygame.K_EQUALS, pygame.K_PLUS):
                        self.zoom = min(self.max_zoom, self.zoom * 1.2)
                    elif event.key in (pygame.K_MINUS, pygame.K_UNDERSCORE):
                        self.zoom = max(self.min_zoom, self.zoom / 1.2)
                    elif event.key == pygame.K_0:
                        self.zoom = 1.0

            # Smooth keyboard panning (Arrow keys or WASD)
            keys = pygame.key.get_pressed()
            pan_speed = 600.0 / self.zoom * dt
            if keys[pygame.K_LEFT] or keys[pygame.K_a]:
                self.cam_x -= pan_speed
            if keys[pygame.K_RIGHT] or keys[pygame.K_d]:
                self.cam_x += pan_speed
            if keys[pygame.K_UP] or keys[pygame.K_w]:
                self.cam_y += pan_speed
            if keys[pygame.K_DOWN] or keys[pygame.K_s]:
                if not (keys[pygame.K_LCTRL] or keys[pygame.K_RCTRL]):
                    self.cam_y -= pan_speed
                    
            self.draw()
            
        pygame.quit()
