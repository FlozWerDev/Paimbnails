# Support Paimbnails

Thank you for using Paimbnails! This page covers help, troubleshooting, bug reporting, and how to support the project.

---

## Support the Development

Paimbnails is developed and maintained by **FlozWer**. If you enjoy the mod and want to help keep it alive and growing, consider supporting via:

- **GitHub Sponsors** — [github.com/sponsors/FlozWerDev](https://github.com/sponsors/FlozWerDev)
- **Patreon** — [patreon.com/FlozWer](https://patreon.com/FlozWer) *(placeholder — update with your real link)*

Supporters get early access to builds, a badge on their profile, and direct input on upcoming features.

---

## Need Help?

### Common Issues

**Thumbnails not loading**
- Check your internet connection.
- Try the cleanup tools in Paimbnails settings: `Maintenance & Moderator` → `Run Mod Cleanup`.
- If using a VPN, the CDN may be blocked — try disabling it temporarily.
- Make sure the mod is updated to the latest version.

**Game lag or stuttering with visual effects**
- Disable heavier effects in `Level Visual Effects` settings.
- Lower **Download Speed (Threads)** under `Performance & Optimization`.
- Disable `Cache GIFs in RAM` if your system is low on memory.
- Disable `Disable Video Chunking` to reduce initial load spikes.

**Profile music not playing**
- Enable **Profile Music** in Paimbnails settings.
- The profile owner must have configured a song fragment.
- Check that your in-game music volume is not muted.

**Custom progress bar not appearing**
- Make sure **Enable Custom Progress Bar** is toggled ON in the pause-menu popup.
- If the bar is off-screen, open the popup and tap **Center Position**.
- Free-drag mode only works while the pause menu is open.

**Mod conflicts or UI overlapping**
- Paimbnails uses late-priority hooks to minimize conflicts.
- If another mod overrides the same UI elements, try changing the load order (disable → re-enable Paimbnails last).
- Report the conflict on Discord with both mod names and versions.

**Capture button not showing in pause menu**
- Ensure **Enable Capture Button** is ON in `Capture Settings`.
- The button appears only while playing a level (PlayLayer).

---

### FAQ

**Q: Can I use my own thumbnails?**  
A: Yes! Use the **In-Game Capture** button in the pause menu, or upload via the web panel (moderators/admins).

**Q: Does this mod work offline?**  
A: Thumbnails and profile data require an internet connection. Most visual effects and local settings work fully offline.

**Q: Will this get me banned?**  
A: No. Paimbnails is purely client-side visual and audio enhancement. It does not modify gameplay mechanics, memory, or level data.

**Q: How do I become a moderator?**  
A: Moderators are hand-picked by the admin team. Join the Discord and contribute to the community to be considered.

**Q: Is the mod open source?**  
A: Yes — the source code is available at [github.com/FlozWerDev/Paimbnails](https://github.com/FlozWerDev/Paimbnails).

---

### Reporting Bugs

1. **Enable debug logs**: Paimbnails settings → `Test / Debug` → `Enable Debug Logs`.
2. **Reproduce** the issue once or twice.
3. Grab the latest log from your Geode logs folder (`<GD folder>/geode/logs/`).
4. Send it on our [Discord server](https://discord.gg/5N5vpSfZwY) or open a GitHub issue:
   - [github.com/FlozWerDev/Paimbnails/issues](https://github.com/FlozWerDev/Paimbnails/issues)

Please include:
- What you were doing when it happened
- Paimbnails version and Geometry Dash version
- Platform (Windows / Android / macOS / iOS)
- The log file or crash report

---

### Known Issues

- Heavy visual effects may cause frame drops on low-end devices when many level cells are visible at once.
- Animated thumbnails, GIFs, and video consume more RAM than static images; long sessions may increase memory usage.
- Audio transitions and media decoding run asynchronously — brief timing hiccups can still occur on slower hardware.
- Custom progress bar free-drag uses pause-layer touch interception and may conflict with other pause-menu mods.

---

### Community & Links

- **Discord**: [discord.gg/5N5vpSfZwY](https://discord.gg/5N5vpSfZwY) — help, feature requests, dev updates, thumbnail submissions
- **GitHub**: [github.com/FlozWerDev/Paimbnails](https://github.com/FlozWerDev/Paimbnails) — source code and bug reports
- **Developer**: FlozWer ([GitHub](https://github.com/FlozWerDev))
