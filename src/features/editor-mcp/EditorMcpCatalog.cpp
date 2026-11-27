#include "EditorMcpCatalog.hpp"
#include "EditorMcpTypes.hpp"

#ifndef PAIM_EDITOR_MCP_HEADLESS
#include <Geode/binding/ObjectToolbox.hpp>
#endif

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>
#include <unordered_set>

namespace paimon::editormcp {

namespace {

std::string toLower(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (unsigned char c : sv) {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

bool containsCI(std::string_view haystack, std::string_view needleLower) {
    if (needleLower.empty()) return true;
    return toLower(haystack).find(needleLower) != std::string::npos;
}

} // namespace

std::vector<TriggerDef> const& triggerCatalog() {
    static std::vector<TriggerDef> const kTriggers = {
        // --- Color & Visual ---
        {899, "Color", "Color & Visual",
         "Changes any color channel (1..999 or 1000=BG, 1001=G1, 1002=Line, 1003=3DL, 1004=Obj, 1009=G2) over duration with optional blending, opacity, player color, or copy color + HSV.",
         "23=targetColorID, 7=R, 8=G, 9=B, 10=duration, 35=opacity, 17=blending, 15=pCol1, 16=pCol2, 50=copyColorID, 49=copyHSV, 60=copyOpacity, 11=touch, 62=spawn, 87=multi"},
        {1006, "Pulse", "Color & Visual",
         "Temporarily flashes/pulses a color channel or group with fade-in, hold, and fade-out. Can pulse color or HSV of another channel/group.",
         "51=targetID(channel or group), 52=targetType(0=channel,1=group), 48=pulseMode(0=color,1=HSV), 7/8/9=RGB, 49=HSV, 50=copyColorID, 45=fadeIn, 46=hold, 47=fadeOut, 65=mainOnly, 66=detailOnly, 86=exclusive"},
        {1007, "Alpha", "Color & Visual",
         "Changes the opacity (0.0..1.0) of all objects in a target group over duration.",
         "51=targetGroupID, 35=opacity(0..1), 10=duration, 11=touch, 62=spawn, 87=multi"},
        {2903, "Gradient", "Color & Visual",
         "Creates a 2-color or 4-corner vertex gradient overlay across a blending layer/mode.",
         "174=gradientID, 203=upLeftColorID, 204=downRightColorID, 205=leftTopLeftID, 206=rightTopRightID, 207=vertexMode, 208=blendingMode, 209=layer, 51=disable/enable"},
        {3029, "BG Change", "Color & Visual",
         "Changes the active background texture index mid-level.",
         "11=touch, 62=spawn"},
        {3030, "Ground Change", "Color & Visual",
         "Changes the active ground texture index mid-level.",
         "11=touch, 62=spawn"},
        {3031, "Middleground Change", "Color & Visual",
         "Changes the active middleground texture index mid-level.",
         "11=touch, 62=spawn"},
        {1818, "BG Effect On", "Color & Visual", "Enables background particle/star effects.", "11=touch, 62=spawn"},
        {1819, "BG Effect Off", "Color & Visual", "Disables background particle/star effects.", "11=touch, 62=spawn"},
        {29, "BG Color (Legacy)", "Color & Visual", "Legacy color trigger targeting channel 1000 (BG).", "7/8/9=RGB, 10=duration, 14=tintGround"},
        {30, "G1 Color (Legacy)", "Color & Visual", "Legacy color trigger targeting channel 1001 (G1).", "7/8/9=RGB, 10=duration"},
        {104, "Line Color (Legacy)", "Color & Visual", "Legacy color trigger targeting channel 1002 (Line).", "7/8/9=RGB, 10=duration"},
        {105, "Obj Color (Legacy)", "Color & Visual", "Legacy color trigger targeting channel 1004 (Obj).", "7/8/9=RGB, 10=duration"},
        {744, "3DL Color (Legacy)", "Color & Visual", "Legacy color trigger targeting channel 1003 (3DL).", "7/8/9=RGB, 10=duration"},
        {900, "G2 Color (Legacy)", "Color & Visual", "Legacy color trigger targeting channel 1009 (G2).", "7/8/9=RGB, 10=duration"},
        {915, "Col1..4 (Legacy)", "Color & Visual", "Legacy color channel trigger.", "7/8/9=RGB, 10=duration"},

        // --- Motion & Transform ---
        {901, "Move", "Motion & Transform",
         "Moves a group by (moveX, moveY) GD units (1 block = 30 units), locks to player/camera X/Y, or moves toward a target group.",
         "51=targetGroupID, 28=moveX, 29=moveY, 10=duration, 30=easing(0..18), 85=easingRate, 58=lockPlayerX, 59=lockPlayerY, 141=lockCameraX, 142=lockCameraY, 100=useTargetMove, 101=targetMode(0=Both,1=X,2=Y), 71=targetPosGroupID, 394=smallStep(1/30), 144=silent"},
        {1346, "Rotate", "Motion & Transform",
         "Rotates target group around center group by degrees + full 360 turns, or aims dynamically at a target.",
         "51=targetGroupID, 71=centerGroupID, 68=degrees, 69=times360, 10=duration, 30=easing, 85=easingRate, 70=lockObjectRotation, 105=aimTargetID, 397=dynamicMode"},
        {1347, "Follow", "Motion & Transform",
         "Makes target group continuously follow center group scaled by (xMod, yMod) for duration.",
         "51=targetGroupID, 71=centerGroupID, 72=xMod, 73=yMod, 10=duration"},
        {1814, "Follow Player Y", "Motion & Transform",
         "Makes target group track the player's Y position with configurable speed, delay, offset, and max speed.",
         "51=targetGroupID, 90=speed, 91=delay, 92=offset, 93=maxSpeed, 10=duration"},
        {2067, "Scale", "Motion & Transform",
         "Scales target group X/Y around center group over duration.",
         "51=targetGroupID, 71=centerGroupID, 150=scaleX, 151=scaleY, 10=duration, 30=easing, 85=easingRate"},
        {3016, "Advanced Follow", "Motion & Transform",
         "Physics-like homing/velocity follower: sets speed, direction, acceleration, friction, max range, and target (group/P1/P2/camera).",
         "51=targetGroupID, 71=followGroupID, 138=p1, 200=p2, 201=camera, 300..320=speed/dir/accel/friction/mode"},
        {3660, "Adv Follow Edit", "Motion & Transform",
         "Modifies parameters of an already-active Advanced Follow instance by group or controlID.",
         "51=targetGroupID, 535=controlID"},
        {3661, "Adv Follow Retarget", "Motion & Transform",
         "Retargets an active Advanced Follow instance to a new center/target group.",
         "51=targetGroupID, 71=newCenterGroupID, 535=controlID"},
        {3032, "Keyframe Point", "Motion & Transform",
         "Defines a keyframe node in a Keyframe animation path (position, rotation, scale, time/easing).",
         "57=groups, 373=animID, 374=index, 375=time, 376=easing"},
        {3033, "Keyframe Animation", "Motion & Transform",
         "Plays a Keyframe Point animation path on a target group.",
         "51=targetGroupID, 71=parentGroupID, 373=animID, 520=timeMod, 521=posXMod, 522=posYMod, 523=rotMod, 524=scaleXMod, 525=scaleYMod"},
        {1585, "Animate", "Motion & Transform",
         "Triggers sprite animation state (monsters, bats, animated objects) on target group.",
         "51=targetGroupID, 76=animationID"},

        // --- Area Effects (2.2) ---
        {3006, "Area Move", "Area Effects", "Applies spatial/proximity X/Y offset to objects in targetGroupID relative to centerGroupID.", "51=targetGroupID, 71=centerGroupID, 220=moveX, 221=moveY, 222=length, 223=offset, 224=easing, 231=modFront, 232=modBack, 11=touch, 62=spawn, 87=multi"},
        {3007, "Area Rotate", "Area Effects", "Applies spatial/proximity rotation to objects in targetGroupID relative to centerGroupID.", "51=targetGroupID, 71=centerGroupID, 220=degrees, 222=length, 223=offset, 224=easing, 231=modFront, 232=modBack, 11=touch, 62=spawn, 87=multi"},
        {3008, "Area Scale", "Area Effects", "Applies spatial/proximity X/Y scaling to objects in targetGroupID relative to centerGroupID.", "51=targetGroupID, 71=centerGroupID, 220=scaleX, 221=scaleY, 222=length, 223=offset, 224=easing, 231=modFront, 232=modBack, 11=touch, 62=spawn, 87=multi"},
        {3009, "Area Fade", "Area Effects", "Applies spatial/proximity opacity change to objects in targetGroupID relative to centerGroupID.", "51=targetGroupID, 71=centerGroupID, 220=fromOpacity, 221=toOpacity, 222=length, 223=offset, 224=easing, 231=modFront, 232=modBack, 11=touch, 62=spawn, 87=multi"},
        {3010, "Area Tint", "Area Effects", "Applies spatial/proximity color/HSV tint to objects in targetGroupID relative to centerGroupID.", "51=targetGroupID, 71=centerGroupID, 222=length, 223=offset, 224=easing, 231=modFront, 232=modBack, 49=HSV, 11=touch, 62=spawn, 87=multi"},
        {3011, "Edit Area Move", "Area Effects", "Modifies parameters of an active Area Move effect by targetGroupID or controlID.", "51=targetGroupID, 220=moveX, 221=moveY, 222=length, 223=offset, 224=easing, 535=useControlID, 11=touch, 62=spawn, 87=multi"},
        {3012, "Edit Area Rotate", "Area Effects", "Modifies parameters of an active Area Rotate effect by targetGroupID or controlID.", "51=targetGroupID, 220=degrees, 222=length, 223=offset, 224=easing, 535=useControlID, 11=touch, 62=spawn, 87=multi"},
        {3013, "Edit Area Scale", "Area Effects", "Modifies parameters of an active Area Scale effect by targetGroupID or controlID.", "51=targetGroupID, 220=scaleX, 221=scaleY, 222=length, 223=offset, 224=easing, 535=useControlID, 11=touch, 62=spawn, 87=multi"},
        {3014, "Edit Area Fade", "Area Effects", "Modifies parameters of an active Area Fade effect by targetGroupID or controlID.", "51=targetGroupID, 220=fromOpacity, 221=toOpacity, 222=length, 223=offset, 224=easing, 535=useControlID, 11=touch, 62=spawn, 87=multi"},
        {3015, "Edit Area Tint", "Area Effects", "Modifies parameters of an active Area Tint effect by targetGroupID or controlID.", "51=targetGroupID, 222=length, 223=offset, 224=easing, 49=HSV, 535=useControlID, 11=touch, 62=spawn, 87=multi"},
        {3024, "Area Stop", "Area Effects", "Stops active Area effects matching targetGroupID or controlID.", "51=targetGroupID, 535=useControlID, 11=touch, 62=spawn, 87=multi"},

        // --- Spawning, Logic & State ---
        {1049, "Toggle", "Spawning & Logic",
         "Activates or deactivates (toggles on/off) all objects in targetGroupID.",
         "51=targetGroupID, 56=activateGroup(0=off,1=on), 11=touch, 62=spawn, 87=multi"},
        {1268, "Spawn", "Spawning & Logic",
         "Activates all spawn-triggered triggers in targetGroupID after optional delay (plus delay variance) and optional group remapping.",
         "51=targetGroupID, 63=delay, 556=delayVariance, 441=previewDisable, 107=spawnOrdered, 442=remapString"},
        {1616, "Stop", "Spawning & Logic",
         "Stops, pauses, or resumes active triggers targeting targetGroupID or controlID.",
         "51=targetGroupID, 580=stopMode(0=stop,1=pause,2=resume), 535=useControlID"},
        {1595, "Touch", "Spawning & Logic",
         "Triggers target group when player taps/clicks (or releases) input, with hold mode and P1/P2 filter.",
         "51=targetGroupID, 81=holdMode, 82=toggleMode(0=normal,1=on,2=off), 89=dualMode, 198=playerControl(0=both,1=P1,2=P2)"},
        {1611, "Count", "Spawning & Logic",
         "Listens to itemID and activates targetGroupID when item reaches targetCount.",
         "80=itemID, 77=targetCount, 51=targetGroupID, 56=activateGroup, 104=multiActivate"},
        {1811, "Instant Count", "Spawning & Logic",
         "Immediately checks if itemID equals/exceeds/is less than targetCount and triggers targetGroupID.",
         "80=itemID, 77=targetCount, 51=targetGroupID, 56=activateGroup, 88=comparison(0=eq,1=gt,2=lt)"},
        {1817, "Pickup", "Spawning & Logic",
         "Adds, subtracts, or overrides itemID count (or activates pickup collectible behavior).",
         "80=itemID, 77=count, 139=overrideMode, 88=multiply/divide"},
        {1815, "Collision", "Spawning & Logic",
         "Activates targetGroupID when Collision Block blockA collides with blockB (or on exit).",
         "80=blockAID, 95=blockBID, 51=targetGroupID, 56=activateGroup, 93=triggerOnExit, 94=targetP1, 200=targetP2"},
        {1816, "Collision Block", "Spawning & Logic",
         "Invisible physics hitbox block used by Collision / Instant Collision triggers.",
         "80=blockID, 94=dynamicBlock(1=dynamic)"},
        {3640, "Collision State Block", "Spawning & Logic",
         "Triggers groups on player collision enter/exit state.",
         "51=targetGroupID"},
        {3609, "Instant Collision", "Spawning & Logic",
         "Immediately tests if blockA overlaps blockB and fires trueGroup or falseGroup.",
         "80=blockAID, 95=blockBID, 51=trueGroupID, 71=falseGroupID"},
        {3607, "Sequence", "Spawning & Logic",
         "Cycles through a list of group:count pairs each time it is triggered (sequential, random, reset modes).",
         "435=sequenceList, 436=minInt, 437=sequenceMode, 438=resetMode, 439=resetTime"},
        {1912, "Random", "Spawning & Logic",
         "Picks group1 or group2 based on chance percentage (0..100%).",
         "51=group1, 71=group2, 10=chancePercent(0..100)"},
        {2068, "Advanced Random", "Spawning & Logic",
         "Weighted random selection across many groupID:weight pairs.",
         "152=chanceList(id.weight...)"},
        {3604, "Event", "Spawning & Logic",
         "Fires targetGroupID on gameplay events (jump, land, hit ceiling, orb, portal, dual, etc.).",
         "51=targetGroupID, 430=eventTypes, 447=extraID, 525=extraID2"},
        {3614, "Time", "Spawning & Logic",
         "Starts/runs a high-precision timer in itemID with startTime, stopTime, targetGroup, and timeMod.",
         "80=itemID, 51=targetGroupID, 467=startTime, 468=stopTime, 469=stopChecked, 470=timeMod, 471=ignoreTimeWarp, 472=startPaused"},
        {3615, "Time Event", "Spawning & Logic",
         "Fires targetGroupID when Timer itemID crosses targetTime.",
         "80=itemID, 51=targetGroupID, 473=targetTime, 474=multiActivate"},
        {3617, "Time Control", "Spawning & Logic",
         "Starts, pauses, or resets a Timer itemID.",
         "80=itemID, 475=startStop(0=start,1=stop)"},
        {3618, "Reset", "Spawning & Logic",
         "Resets a group's position/state or persistent item.",
         "51=targetGroupID"},
        {3619, "Item Edit", "Spawning & Logic",
         "Performs arithmetic (+, -, *, /) on item1 and item2/mod and writes result to targetItem (supports float, timer, round, abs, sign).",
         "80=item1, 95=item2, 51=targetItem, 479=mod1, 480=op1, 481=op2, 482=mod2, 476=item1Type, 477=item2Type, 478=targetType"},
        {3620, "Item Compare", "Spawning & Logic",
         "Compares item1 vs item2 (==, >, >=, <, <=, !=) and triggers trueGroupID or falseGroupID.",
         "80=item1, 95=item2, 51=trueGroupID, 71=falseGroupID, 482=comparison, 483=tolerance"},
        {3641, "Persistent Item", "Spawning & Logic",
         "Marks an itemID or timer as persistent across attempts or resets it.",
         "80=itemID, 491=persistent, 492=targetAll, 493=reset, 494=timer"},
        {3655, "Object Control", "Spawning & Logic",
         "Applies special control actions to target group objects.",
         "51=targetGroupID"},
        {3662, "Link Visible", "Spawning & Logic",
         "Links visibility across target group objects.",
         "51=targetGroupID"},
        {1829, "Spawn Particle", "Spawning & Logic",
         "Spawns a custom particle burst at target/position.",
         "51=targetGroupID, 71=posGroupID"},

        // --- Camera (2.2) ---
        {1913, "Camera Zoom", "Camera",
         "Zooms the gameplay camera to zoomValue over duration with easing.",
         "371=zoomValue, 10=duration, 30=easing, 85=easingRate"},
        {1914, "Camera Static", "Camera",
         "Locks the gameplay camera onto targetGroupID (or exits static mode) with optional smooth follow & easing.",
         "51=targetGroupID, 110=exitStatic, 10=duration, 30=easing, 85=easingRate, 212=followObject, 213=followEasing, 111=exitInstant"},
        {1916, "Camera Offset", "Camera",
         "Offsets the gameplay camera in X and/or Y over duration with easing.",
         "28=offsetX, 29=offsetY, 10=duration, 30=easing, 85=easingRate, 101=targetMode"},
        {1915, "Camera Rotate (Gameplay)", "Camera",
         "Rotates gameplay direction / camera angle.",
         "68=degrees, 10=duration, 30=easing"},
        {2015, "Camera Rotate", "Camera",
         "Rotates the camera view by degrees (or snaps/adds) over duration.",
         "68=degrees, 10=duration, 30=easing, 85=easingRate, 394=snap360"},
        {2062, "Camera Edge", "Camera",
         "Sets a hard camera boundary edge (left, right, top, bottom) from targetGroupID.",
         "51=targetGroupID, 164=edgeDirection(1=left,2=right,3=top,4=bottom)"},
        {2925, "Camera Mode", "Camera",
         "Configures free camera mode, easing, padding, and grid snap settings.",
         "112=freeMode, 113=editSettings, 114=easing, 115=padding, 370=disableGridSnap"},
        {2016, "Camera Guide", "Camera",
         "Visual preview box in the editor showing exact camera frame at that position/zoom.",
         "371=zoom, 28=offsetX, 29=offsetY"},
        {1520, "Shake", "Camera",
         "Shakes the screen with strength and interval over duration.",
         "75=strength, 76=interval, 10=duration"},

        // --- Shaders & Post-Processing (2.2) ---
        {2904, "Shader (Setup)", "Shaders", "Configures active shader Z-layer range (minLayer..maxLayer).", "175=minLayer, 176=maxLayer, 51=targetGroupID, 11=touch, 62=spawn, 87=multi"},
        {2905, "Shock Wave", "Shaders", "Circular shock wave distortion shader.", "10=duration, 30=easing, 85=easingRate, 51=targetGroupID, 11=touch, 62=spawn, 87=multi"},
        {2907, "Shock Line", "Shaders", "Linear shock wave distortion shader.", "10=duration, 30=easing, 85=easingRate, 51=targetGroupID, 11=touch, 62=spawn, 87=multi"},
        {2909, "Glitch", "Shaders", "Digital glitch displacement & RGB split shader.", "10=duration, 30=easing, 85=easingRate, 11=touch, 62=spawn, 87=multi"},
        {2910, "Chromatic", "Shaders", "Chromatic aberration RGB offset shader.", "10=duration, 30=easing, 85=easingRate, 11=touch, 62=spawn, 87=multi"},
        {2911, "Chromatic Glitch", "Shaders", "Animated chromatic glitch shader.", "10=duration, 30=easing, 85=easingRate, 11=touch, 62=spawn, 87=multi"},
        {2912, "Pixelate", "Shaders", "Pixelates rendered output to target pixel size.", "10=duration, 30=easing, 85=easingRate, 11=touch, 62=spawn, 87=multi"},
        {2913, "Lens Circle", "Shaders", "Circular vignette / lens distortion shader.", "10=duration, 30=easing, 85=easingRate, 11=touch, 62=spawn, 87=multi"},
        {2914, "Radial Blur", "Shaders", "Radial zoom blur centered on screen or target group.", "10=duration, 30=easing, 85=easingRate, 51=targetGroupID, 11=touch, 62=spawn, 87=multi"},
        {2915, "Motion Blur", "Shaders", "Directional motion blur shader.", "10=duration, 30=easing, 85=easingRate, 11=touch, 62=spawn, 87=multi"},
        {2916, "Bulge", "Shaders", "Convex radial bulge distortion shader.", "10=duration, 30=easing, 85=easingRate, 51=targetGroupID, 11=touch, 62=spawn, 87=multi"},
        {2917, "Pinch", "Shaders", "Concave pinch warp distortion shader.", "10=duration, 30=easing, 85=easingRate, 51=targetGroupID, 11=touch, 62=spawn, 87=multi"},
        {2919, "Grayscale", "Shaders", "Desaturates rendered output toward grayscale.", "10=duration, 30=easing, 85=easingRate, 11=touch, 62=spawn, 87=multi"},
        {2920, "Sepia", "Shaders", "Applies sepia tone color transform.", "10=duration, 30=easing, 85=easingRate, 11=touch, 62=spawn, 87=multi"},
        {2921, "Invert Color", "Shaders", "Inverts screen RGB channels.", "10=duration, 30=easing, 85=easingRate, 11=touch, 62=spawn, 87=multi"},
        {2922, "Hue", "Shaders", "Rotates hue of rendered layers.", "10=duration, 30=easing, 85=easingRate, 11=touch, 62=spawn, 87=multi"},
        {2923, "Edit Hue/Color", "Shaders", "Adjusts brightness/contrast/saturation.", "10=duration, 30=easing, 85=easingRate, 11=touch, 62=spawn, 87=multi"},
        {2924, "Split Screen", "Shaders", "Splits/tiles the rendered viewport.", "10=duration, 30=easing, 85=easingRate, 11=touch, 62=spawn, 87=multi"},

        // --- Audio & Music (2.2) ---
        {1934, "Song", "Audio", "Starts or switches to a song channel/song ID with fade, speed, volume, and millisecond offset.", "392=songChannel, 404=songID, 405=speed, 406=volume, 408=startMs, 409=fadeIn, 410=endMs, 411=fadeOut, 412=loop, 11=touch, 62=spawn, 87=multi"},
        {3605, "Edit Song", "Audio", "Modifies volume, speed, or stop state on an active song channel.", "392=songChannel, 405=speed, 406=volume, 10=duration, 11=touch, 62=spawn, 87=multi"},
        {3602, "SFX", "Audio", "Plays an SFX ID with configurable pitch, speed, volume, start/end trim, fade, loop, reverb, and proximity group.", "392=soundID, 405=speed, 406=volume, 407=pitch, 408=startMs, 409=fadeIn, 410=endMs, 411=fadeOut, 412=loop, 415=reverb, 421=proximityGroupID, 11=touch, 62=spawn, 87=multi"},
        {3603, "Edit SFX", "Audio", "Modifies volume/speed/pitch or stops an active SFX group.", "392=sfxGroup, 405=speed, 406=volume, 407=pitch, 10=duration, 11=touch, 62=spawn, 87=multi"},
        {3606, "Audio Line Guide", "Audio", "Draws a duration/sync guide bar in the editor.", "10=duration"},
        {3608, "BPM Guide", "Audio", "Draws BPM beat guidelines in the editor.", "498=bpm, 499=beatsPerBar"},

        // --- Gameplay, Player & Level Flow ---
        {1917, "Reverse", "Gameplay & Player", "Reverses horizontal gameplay movement direction.", "11=touch, 62=spawn, 87=multi"},
        {1932, "Player Control", "Gameplay & Player", "Overrides or halts P1/P2 movement or jump state.", "138=p1, 200=p2, 11=touch, 62=spawn, 87=multi"},
        {1935, "Timewarp", "Gameplay & Player", "Scales game simulation speed multiplier (0.1x .. 2.0x).", "120=timeMod, 11=touch, 62=spawn, 87=multi"},
        {2066, "Gravity", "Gameplay & Player", "Sets player gravity multiplier.", "148=gravityMod, 11=touch, 62=spawn, 87=multi"},
        {2899, "Options", "Gameplay & Player", "Toggles level state flags mid-run (hide player, disable trail, hide particles, etc.).", "160..170=optionFlags, 11=touch, 62=spawn, 87=multi"},
        {2900, "Rotate Gameplay", "Gameplay & Player", "Rotates the gameplay movement/gravity axis by degrees.", "68=degrees, 11=touch, 62=spawn, 87=multi"},
        {2999, "Setup MG / Camera", "Gameplay & Player", "Adjusts middleground speed and offset.", "51=target, 11=touch, 62=spawn, 87=multi"},
        {3022, "Teleport (Trigger)", "Gameplay & Player", "Teleports player to targetGroupID with configurable smooth/gravity/velocity preservation.", "51=targetGroupID, 353=smooth, 11=touch, 62=spawn, 87=multi"},
        {3600, "End", "Gameplay & Player", "Ends the level immediately or moves end screen target to targetGroupID.", "51=targetGroupID, 444=noEffects, 445=noSFX, 446=instant, 11=touch, 62=spawn"},
        {3612, "Platformer End / Checkpoint", "Gameplay & Player", "Platformer checkpoint / end control.", "51=targetGroupID, 11=touch, 62=spawn"},
        {3613, "UI", "Gameplay & Player", "Pins targetGroupID to screen-space UI coordinates anchored relative to centerGroupID/screen references.", "51=targetGroupID, 71=centerGroupID, 464=xRef, 465=yRef, 11=touch, 62=spawn"},
        {3643, "Show / Hide Player", "Gameplay & Player", "Sets player sprite visibility (legacy 1612=Hide, 1613=Show).", "11=touch, 62=spawn, 87=multi"},
        {1612, "Hide Player", "Gameplay & Player", "Hides the player icon.", "11=touch, 62=spawn, 87=multi"},
        {1613, "Show Player", "Gameplay & Player", "Shows the player icon.", "11=touch, 62=spawn, 87=multi"},
        {1812, "Disable Trail", "Gameplay & Player", "Disables player trail.", "11=touch, 62=spawn, 87=multi"},
        {1813, "Enable Trail", "Gameplay & Player", "Enables player trail.", "11=touch, 62=spawn, 87=multi"},
        {1859, "BG Effect / Spawn Helper", "Gameplay & Player", "Toggles BG effect or activates targetGroupID.", "51=targetGroupID, 11=touch, 62=spawn, 87=multi"},
        {31, "Start Pos", "Gameplay & Player", "Playtest start position with configurable gamemode, speed, mini, dual, mirror, resetCamera.", "2=x, 3=y, kA2=mode, kA4=speed, kA3=mini, kA8=dual"},

        // --- Enter / Exit Transitions (2.2) ---
        {22, "Enter Effect: Fade", "Enter Effects", "Built-in object enter effect.", ""},
        {23, "Enter Effect: Move Up", "Enter Effects", "Built-in object enter effect.", ""},
        {24, "Enter Effect: Move Down", "Enter Effects", "Built-in object enter effect.", ""},
        {25, "Enter Effect: Scale", "Enter Effects", "Built-in object enter effect.", ""},
        {26, "Enter Effect: Scale Out", "Enter Effects", "Built-in object enter effect.", ""},
        {27, "Enter Effect: Random", "Enter Effects", "Built-in object enter effect.", ""},
        {28, "Enter Effect: Bounce", "Enter Effects", "Built-in object enter effect.", ""},
        {55, "Enter Effect: None", "Enter Effects", "Disables enter effect.", ""},
        {56, "Enter Effect: Rotate", "Enter Effects", "Built-in object enter effect.", ""},
        {57, "Enter Effect: Scale X", "Enter Effects", "Built-in object enter effect.", ""},
        {58, "Enter Effect: Scale Y", "Enter Effects", "Built-in object enter effect.", ""},
        {59, "Enter Effect: Spin", "Enter Effects", "Built-in object enter effect.", ""},
        {1931, "Enter Stop", "Enter Effects", "Stops enter/exit transitions on target group.", "51=targetGroupID"},
        {3017, "Enter Move", "Enter Effects", "Custom 2.2 enter/exit move transition.", "51=targetGroupID"},
        {3018, "Enter Rotate", "Enter Effects", "Custom 2.2 enter/exit rotate transition.", "51=targetGroupID"},
        {3019, "Enter Scale", "Enter Effects", "Custom 2.2 enter/exit scale transition.", "51=targetGroupID"},
        {3020, "Enter Fade", "Enter Effects", "Custom 2.2 enter/exit fade transition.", "51=targetGroupID"},
        {3021, "Enter Tint", "Enter Effects", "Custom 2.2 enter/exit tint transition.", "51=targetGroupID"},
    };
    return kTriggers;
}

std::vector<CuratedObjectDef> const& curatedObjectCatalog() {
    static std::vector<CuratedObjectDef> const kObjects = {
        // Basic Blocks & Solids (1 block = 30x30 GD units)
        {1, "Default Block", "Blocks", "Classic 30x30 solid block"},
        {2, "Brick Block", "Blocks", "30x30 solid brick outline block"},
        {3, "Grid Block", "Blocks", "30x30 solid grid block"},
        {4, "Bordered Block", "Blocks", "30x30 solid bordered block"},
        {5, "Checkered Block", "Blocks", "30x30 checkered block"},
        {6, "Cross Block", "Blocks", "30x30 X-cross block"},
        {7, "Dot Grid Block", "Blocks", "30x30 dotted block"},
        {211, "White Colorable Square", "Blocks / Deco", "Solid white 30x30 colorable square (best for custom geometry & pixel art!)"},
        {467, "1.9 Half-Slab", "Slabs", "30x15 solid half slab"},
        {468, "Flat Slab", "Slabs", "30x15 solid flat slab"},
        {469, "Outline Slab", "Slabs", "30x15 outline slab"},
        {470, "Deco Slab", "Slabs", "30x15 deco slab"},
        {693, "White Triangle (30x30)", "Deco Shapes", "Solid white borderless 30x30 right triangle (90-deg corner at bottom-right when rot=0; rot=-135 points 90-deg apex UP, +45 DOWN, +135 LEFT, -45 RIGHT)"},
        {694, "White Wide Triangle (60x30)", "Deco Shapes", "Solid white borderless 60x30 right triangle (asymmetric 90-deg corner; prefer 693 or 216 for symmetric points)"},
        {1764, "White Circle (Filled)", "Deco Shapes", "Small solid colorable circle"},
        {3637, "Colorable Circle (50x50)", "Deco Shapes", "Smooth large borderless colorable circle (50x50 GD units at scale=1.0; scale_x/scale_y make crisp ovals)"},
        {3636, "Colorable Ring (Thick)", "Deco Shapes", "Thick hollow colorable circle ring (good for bows/loops)"},
        {997, "Ring Arc Segment 1", "Deco Shapes", "Curved ring arc segment (~90 deg)"},
        {998, "Ring Arc Segment 2", "Deco Shapes", "Long curved ring arc segment (~220 deg, great for halos/crescents/ahoge)"},
        {999, "Ring Arc Segment 3", "Deco Shapes", "Short curved ring arc segment (~75 deg)"},
        {1000, "Ring Arc Segment 4", "Deco Shapes", "Tiny curved ring arc segment (~45 deg)"},
        {507, "Perspective Curve / Line Segment 1", "Deco Shapes", "Thin vector stroke segment (persp_outline_02); chain at 0.12x-0.25x scale for organic character lineart, hair, and folds"},
        {508, "Perspective Curve / Line Segment 2", "Deco Shapes", "Curved vector stroke segment (persp_outline_03); pairs with 507 for detailed art contours"},
        {1009, "Compact Soft Gradient", "Glow & Deco", "10x10 smooth gradient (d_gradient_b_02); ideal for cel-shaded inner gradients, hair/cloth shading, and soft bevel bands"},
        {1202, "Thick Outline Block", "Blocks / Deco", "Thick crisp block outline (blockOutlineThick_01) for bold structural rims"},
        {1339, "Bevel / Slope Outline", "Blocks / Deco", "Clean structural outline segment (blockOutline_15) for unified macro-block edges"},
        {1759, "Structural Outline Variant", "Blocks / Deco", "Clean blockOutline_15 variant for angled architectural frames"},
        {1835, "Scale Fade Ring 1", "Glow & Deco", "Soft radial ring/star bloom (d_scaleFadeRing_01) for specular corner flares and ornaments"},
        {1837, "Scale Fade Ring 2", "Glow & Deco", "Soft radial bloom (d_scaleFadeRing_02) for luminous highlights"},
        {1838, "Scale Fade Ring 4", "Glow & Deco", "Compact radial fade ring (d_scaleFadeRing_04) for soft cel-shading and mist puffs"},
        {129, "Soft Cloud 3", "Atmosphere", "Organic cloud puff (d_cloud_03) for sky framing, foreground snow/mist, and soft silhouette edges"},
        {131, "Soft Cloud 5", "Atmosphere", "Wide cloud puff (d_cloud_05) for layered atmospheric clouds and fog banks"},
        {914, "Text Object", "Text", "Displays text in-level. Key 31 = base64-encoded UTF-8 string."},
        {1615, "Counter / Item Label", "UI / Text", "Displays an itemID / timer / points counter in-level (key 80 = itemID)."},

        // Hazards / Spikes / Saws
        {8, "Default Spike", "Spikes", "Standard 30x30 upward spike"},
        {39, "Small Spike", "Spikes", "Half-height 30x15 spike"},
        {103, "Mini Spike", "Spikes", "Tiny spike"},
        {9, "Floor Spike", "Spikes", "Low floor hazard spike"},
        {144, "Detailed Spike", "Spikes", "Colorable detailed spike"},
        {145, "Small Detailed Spike", "Spikes", "Small colorable spike"},
        {216, "Flat Spike", "Spikes", "Symmetric isosceles triangle (baseColor=outline, detailColor=fill; texture has a thin baked border so avoid for seamless white fills)"},
        {88, "Large Sawblade", "Saws", "Spinning sawblade hazard"},
        {89, "Medium Sawblade", "Saws", "Medium spinning sawblade"},
        {98, "Small Sawblade", "Saws", "Small spinning sawblade"},
        {1705, "Custom Sawblade 1", "Saws", "Colorable 2.1 sawblade"},
        {1706, "Custom Sawblade 2", "Saws", "Medium colorable 2.1 sawblade"},
        {1707, "Custom Sawblade 3", "Saws", "Small colorable 2.1 sawblade"},

        // Slopes
        {289, "Slope 45 deg", "Slopes", "Standard 1x1 solid slope"},
        {290, "Slope 26.5 deg (2x1)", "Slopes", "Wide 2x1 solid slope"},
        {291, "Slope 45 deg (Outlined)", "Slopes", "Outlined 1x1 slope"},
        {292, "Slope 26.5 deg (Outlined)", "Slopes", "Outlined 2x1 slope"},
        {695, "White Slope 45 deg", "Slopes", "Colorable white 1x1 slope"},
        {696, "White Slope 26.5 deg", "Slopes", "Colorable white 2x1 slope"},

        // Orbs & Pads
        {35, "Yellow Jump Pad", "Pads", "Standard yellow jump pad"},
        {140, "Pink Jump Pad", "Pads", "Low-height pink jump pad"},
        {67, "Blue Gravity Pad", "Pads", "Flips gravity on touch"},
        {1332, "Red Jump Pad", "Pads", "High-height red jump pad"},
        {2866, "Spider Pad", "Pads", "Instant spider ceiling/floor snap pad"},
        {36, "Yellow Jump Ring (Orb)", "Orbs", "Standard yellow orb"},
        {141, "Pink Jump Ring (Orb)", "Orbs", "Low pink orb"},
        {84, "Blue Gravity Ring (Orb)", "Orbs", "Gravity-flipping blue orb"},
        {1022, "Green Jump Ring (Orb)", "Orbs", "Gravity-flipping + jump green orb"},
        {1333, "Red Jump Ring (Orb)", "Orbs", "High red orb"},
        {1330, "Black Drop Ring (Orb)", "Orbs", "Slams player down/up"},
        {1704, "Green Dash Orb", "Orbs", "Horizontal/angled dash orb"},
        {1751, "Pink Gravity Dash Orb", "Orbs", "Dash orb that flips gravity"},
        {3004, "Spider Orb", "Orbs", "Instant spider warp orb"},
        {3027, "Teleport Orb", "Orbs", "Teleports player"},

        // Portals (Gamemode, Speed, Size, Dual, Gravity, Mirror)
        {10, "Blue Gravity Portal", "Portals", "Normal gravity"},
        {11, "Yellow Gravity Portal", "Portals", "Inverted gravity"},
        {12, "Cube Portal", "Portals", "Switch to Cube"},
        {13, "Ship Portal", "Portals", "Switch to Ship"},
        {47, "Ball Portal", "Portals", "Switch to Ball"},
        {111, "UFO Portal", "Portals", "Switch to UFO"},
        {660, "Wave Portal", "Portals", "Switch to Wave"},
        {745, "Robot Portal", "Portals", "Switch to Robot"},
        {1331, "Spider Portal", "Portals", "Switch to Spider"},
        {1933, "Swing Portal", "Portals", "Switch to Swing (2.2)"},
        {99, "Regular Size Portal", "Portals", "Normal size"},
        {101, "Mini Size Portal", "Portals", "Mini size"},
        {286, "Dual Portal", "Portals", "Enable dual mode"},
        {287, "Solo Portal", "Portals", "Disable dual mode"},
        {45, "Mirror Portal (On)", "Portals", "Enable mirror"},
        {46, "Mirror Portal (Off)", "Portals", "Disable mirror"},
        {200, "Speed 0.5x (Slow)", "Speed Portals", "Slow speed"},
        {201, "Speed 1x (Normal)", "Speed Portals", "Normal speed"},
        {202, "Speed 2x (Fast)", "Speed Portals", "Fast speed"},
        {203, "Speed 3x (Faster)", "Speed Portals", "Faster speed"},
        {1334, "Speed 4x (Fastest)", "Speed Portals", "Fastest speed"},
        {747, "Teleport Portal (In)", "Portals", "Paired teleport entrance"},
        {749, "Teleport Portal (Out)", "Portals", "Paired teleport exit"},

        // Collectibles & Glow & Decoration
        {142, "Secret Coin", "Collectibles", "Official secret coin"},
        {1329, "User Coin", "Collectibles", "User-created silver coin"},
        {1275, "Collectible Key", "Collectibles", "Pickup key / collectible item"},
        {1587, "Skull Collectible", "Collectibles", "Pickup skull"},
        {1888, "Glow Strip (Long)", "Glow & Deco", "Note: renders emptyFrame.png in some toolboxes; prefer 503 (d_gradient_01) for reliable linear glow"},
        {1011, "Glow Corner", "Glow & Deco", "Corner glow piece"},
        {1012, "Glow Edge", "Glow & Deco", "Edge glow piece"},
        {1765, "Radial Glow Circle", "Glow & Deco", "Caution: d_small_ball_02 can render as an X marker in 2.2; use 3637 with low-opacity blending or 503 for clean glow"},
        {503, "Glow Beam", "Glow & Deco", "Reliable linear soft gradient beam (d_gradient_01_001.png; avoid additive blending on bright/cream BGs)"},
        {917, "Pixel Block (Tiny)", "Pixel Art", "2.2 / tiny pixel deco block"},
        {3062, "2.2 Pixel Art Block", "Pixel Art", "2.2 pixel block"},
        {2065, "Custom Particle Object", "Particles", "2.2 Particle emitter object"},
    };
    return kObjects;
}

std::vector<PropertyKeyDef> const& propertyKeyCatalog() {
    static std::vector<PropertyKeyDef> const kProps = {
        {1, "objectID", "int", "Object type ID (e.g. 1=block, 8=spike, 901=move trigger)."},
        {2, "x", "float", "World X position in GD units (1 block = 30 units)."},
        {3, "y", "float", "World Y position in GD units (ground surface = 0, block center = 15, 45, 75...)."},
        {4, "flipX", "bool", "Horizontal flip (0 or 1)."},
        {5, "flipY", "bool", "Vertical flip (0 or 1)."},
        {6, "rotation", "float", "Clockwise rotation in degrees."},
        {7, "red", "int", "Trigger/color red channel (0..255)."},
        {8, "green", "int", "Trigger/color green channel (0..255)."},
        {9, "blue", "int", "Trigger/color blue channel (0..255)."},
        {10, "duration", "float", "Trigger duration in seconds (or chance% for Random)."},
        {11, "touchTriggered", "bool", "Trigger activates on player touch (0 or 1)."},
        {14, "tintGround", "bool", "Legacy BG trigger tints ground."},
        {15, "playerColor1", "bool", "Use Player 1 color."},
        {16, "playerColor2", "bool", "Use Player 2 color."},
        {17, "blending", "bool", "Additive blending for color/pulse."},
        {20, "editorLayer1", "int", "Primary editor layer (0..N)."},
        {21, "mainColorID", "int", "Base/Main color channel (1..999, or 1000..1012 special)."},
        {22, "detailColorID", "int", "Detail/Secondary color channel."},
        {23, "targetColorID", "int", "Color trigger target channel ID."},
        {24, "zLayer", "int", "Z-Layer: -5=B5, -3=B4, -1=B3, 1=B2, 3=B1, 0=Default, 5=T1, 7=T2, 9=T3, 11=T4."},
        {25, "zOrder", "int", "Draw order within Z-Layer (-100..100+)."},
        {28, "moveX", "float", "Move/Camera Offset X in GD units (30 units = 1 block)."},
        {29, "moveY", "float", "Move/Camera Offset Y in GD units."},
        {30, "easing", "int", "Easing curve ID (0=None .. 18=BackOut)."},
        {31, "text", "base64", "Base64-encoded UTF-8 string for Text Object (ID 914)."},
        {32, "scale", "float", "Uniform object scale (1.0 = default)."},
        {34, "groupParent", "bool", "Marks object as Group Parent for rotation/scale pivots."},
        {35, "opacity", "float", "Trigger/Alpha opacity (0.0 .. 1.0)."},
        {41, "mainHSVEnabled", "bool", "Enable HSV shift on main color."},
        {42, "detailHSVEnabled", "bool", "Enable HSV shift on detail color."},
        {43, "mainHSV", "string", "Main HSV string: 'hSaSvSaddSaddV' e.g. '0a1a1a0a0'."},
        {44, "detailHSV", "string", "Detail HSV string."},
        {45, "fadeIn", "float", "Pulse trigger fade-in duration (s)."},
        {46, "hold", "float", "Pulse trigger hold duration (s)."},
        {47, "fadeOut", "float", "Pulse trigger fade-out duration (s)."},
        {48, "pulseMode", "int", "0=Color, 1=HSV."},
        {49, "copyHSV", "string", "HSV modification string for Color/Pulse triggers."},
        {50, "copyColorID", "int", "Channel ID to copy color from."},
        {51, "targetGroupID", "int", "Target Group ID (1..9999)."},
        {52, "pulseTargetType", "int", "0=Channel, 1=Group."},
        {56, "activateGroup", "bool", "Toggle/Count/Collision enable(1) or disable(0)."},
        {57, "groups", "dot-list", "Assigned group IDs separated by dots, e.g. '1.2.15'."},
        {58, "lockPlayerX", "bool", "Move trigger lock to player X."},
        {59, "lockPlayerY", "bool", "Move trigger lock to player Y."},
        {60, "copyOpacity", "bool", "Copy opacity from copyColorID."},
        {61, "editorLayer2", "int", "Secondary editor layer."},
        {62, "spawnTriggered", "bool", "Trigger only fires when activated by a Spawn/Touch/Collision/Event trigger."},
        {63, "spawnDelay", "float", "Spawn trigger delay in seconds."},
        {64, "dontFade", "bool", "Disable screen-edge fade effect."},
        {65, "mainOnly", "bool", "Pulse main color only."},
        {66, "detailOnly", "bool", "Pulse detail color only."},
        {67, "dontEnter", "bool", "Disable enter transition effect."},
        {68, "rotateDegrees", "float", "Rotate trigger angle in degrees."},
        {69, "times360", "int", "Rotate trigger full 360 rotations."},
        {70, "lockObjectRotation", "bool", "Keep objects upright while rotating around center."},
        {71, "centerGroupID", "int", "Center/Secondary Group ID for Rotate, Follow, Move Target, etc."},
        {72, "followXMod", "float", "Follow trigger X multiplier."},
        {73, "followYMod", "float", "Follow trigger Y multiplier."},
        {75, "shakeStrength", "float", "Shake trigger intensity."},
        {76, "shakeInterval", "float", "Shake trigger interval (or Animate trigger animID)."},
        {77, "targetCount", "int", "Count/Pickup target count."},
        {80, "itemID", "int", "Item / Timer / Collision Block ID."},
        {81, "touchHoldMode", "bool", "Touch trigger triggers on hold/release."},
        {82, "touchToggleMode", "int", "0=Normal, 1=Toggle On, 2=Toggle Off."},
        {85, "easingRate", "float", "Easing curve power/rate (default 2.0)."},
        {86, "exclusive", "bool", "Pulse overrides other active pulses."},
        {87, "multiTrigger", "bool", "Allow trigger to fire multiple times via spawn/touch."},
        {88, "comparison", "int", "Instant Count comparison (0==, 1>=, 2<=)."},
        {89, "touchDualMode", "bool", "Touch trigger dual mode."},
        {95, "blockBID / itemID2", "int", "Secondary block/item ID."},
        {100, "useTargetMove", "bool", "Move trigger moves to target group."},
        {101, "targetMode", "int", "0=Both, 1=X only, 2=Y only."},
        {103, "highDetail", "bool", "High Detail object (hidden when LDM is active)."},
        {104, "multiActivate", "bool", "Count/Timer multi-activate."},
        {107, "spawnOrdered", "bool", "Spawn trigger triggers children in X order."},
        {108, "linkedGroupID", "int", "Linked object group ID."},
        {115, "ord", "int", "Trigger order channel."},
        {116, "channel", "int", "Trigger channel ID."},
        {121, "noTouch", "bool", "Disable hitbox / collision on object (0 or 1)."},
        {128, "scaleX", "float", "Independent X scale (2.2)."},
        {129, "scaleY", "float", "Independent Y scale (2.2)."},
        {131, "warpY", "float", "Skew/warp Y angle (2.2)."},
        {132, "warpX", "float", "Skew/warp X angle (2.2)."},
        {134, "passable", "bool", "Allow passing through bottom/sides."},
        {135, "hide", "bool", "Hide object visually in gameplay."},
        {136, "nonStickX", "bool", "Platformer non-stick X."},
        {137, "extraSticky", "bool", "Platformer extra sticky."},
        {141, "lockCameraX", "bool", "Move trigger lock to camera X."},
        {142, "lockCameraY", "bool", "Move trigger lock to camera Y."},
        {144, "silent", "bool", "Silent move (no player carry momentum)."},
        {371, "zoomValue", "float", "Camera Zoom value."},
        {394, "smallStep", "bool", "Move X/Y is in 1/30th block increments."},
        {441, "previewDisable", "bool", "Hide trigger preview in editor."},
        {495, "enterChannel", "int", "Enter effect channel."},
        {535, "useControlID", "bool", "Target control ID instead of group ID."},
    };
    return kProps;
}

int resolveTriggerId(std::string_view nameOrId) {
    if (nameOrId.empty()) return 0;
    int parsed = 0;
    auto const* begin = nameOrId.data();
    auto const* end = begin + nameOrId.size();
    auto res = std::from_chars(begin, end, parsed);
    if (res.ec == std::errc{} && res.ptr == end && parsed > 0) {
        return parsed;
    }

    std::string norm;
    norm.reserve(nameOrId.size());
    for (unsigned char c : nameOrId) {
        if (std::isalnum(c)) norm.push_back(static_cast<char>(std::tolower(c)));
    }

    struct Alias { char const* key; int id; };
    static constexpr Alias kAliases[] = {
        {"color", 899}, {"col", 899},
        {"pulse", 1006},
        {"alpha", 1007},
        {"gradient", 2903},
        {"move", 901},
        {"rotate", 1346},
        {"follow", 1347},
        {"followplayery", 1814}, {"followy", 1814},
        {"scale", 2067},
        {"toggle", 1049},
        {"spawn", 1268},
        {"stop", 1616},
        {"touch", 1595},
        {"count", 1611},
        {"instantcount", 1811},
        {"pickup", 1817},
        {"collision", 1815},
        {"collisionblock", 1816},
        {"instantcollision", 3609},
        {"collisionstate", 3640},
        {"animate", 1585},
        {"shake", 1520},
        {"advancedfollow", 3016}, {"advfollow", 3016},
        {"advfollowedit", 3660},
        {"advfollowretarget", 3661},
        {"keyframe", 3032}, {"keyframepoint", 3032},
        {"keyframeanim", 3033}, {"keyframeanimation", 3033},
        {"sequence", 3607},
        {"random", 1912},
        {"advancedrandom", 2068}, {"advrandom", 2068},
        {"event", 3604},
        {"time", 3614}, {"timer", 3614},
        {"timeevent", 3615},
        {"timecontrol", 3617},
        {"reset", 3618},
        {"itemedit", 3619},
        {"itemcompare", 3620},
        {"persistentitem", 3641},
        {"objectcontrol", 3655},
        {"linkvisible", 3662},
        {"camerazoom", 1913}, {"zoom", 1913},
        {"camerastatic", 1914}, {"staticcamera", 1914},
        {"cameraoffset", 1916}, {"offsetcamera", 1916},
        {"camerarotate", 2015},
        {"cameraedge", 2062},
        {"cameramode", 2925},
        {"cameraguide", 2016},
        {"song", 1934},
        {"editsong", 3605},
        {"sfx", 3602},
        {"editsfx", 3603},
        {"timewarp", 1935},
        {"reverse", 1917},
        {"gravity", 2066},
        {"playercontrol", 1932},
        {"options", 2899},
        {"rotategameplay", 2900},
        {"teleport", 3022},
        {"end", 3600},
        {"ui", 3613},
        {"hideplayer", 1612},
        {"showplayer", 1613},
        {"disabletrail", 1812},
        {"enabletrail", 1813},
        {"spawnparticle", 1829},
        {"bgchange", 3029},
        {"groundchange", 3030},
        {"mgchange", 3031},
        {"areamove", 3006},
        {"arearotate", 3007},
        {"areascale", 3008},
        {"areafade", 3009},
        {"areatint", 3010},
        {"areastop", 3024},
        {"shockwave", 2905},
        {"shader", 2904}, {"shadersetup", 2904},
        {"shockline", 2907},
        {"glitch", 2909},
        {"chromatic", 2910},
        {"chromaticglitch", 2911},
        {"pixelate", 2912},
        {"lenscircle", 2913},
        {"radialblur", 2914},
        {"motionblur", 2915},
        {"bulge", 2916},
        {"pinch", 2917},
        {"grayscale", 2919},
        {"sepia", 2920},
        {"invertcolor", 2921},
        {"hue", 2922},
        {"startpos", 31},
    };
    for (auto const& a : kAliases) {
        if (norm == a.key) return a.id;
    }

    for (auto const& t : triggerCatalog()) {
        std::string tNorm;
        for (unsigned char c : std::string_view(t.name)) {
            if (std::isalnum(c)) tNorm.push_back(static_cast<char>(std::tolower(c)));
        }
        if (tNorm == norm) return t.id;
    }
    return 0;
}

matjson::Value buildReferenceJson(std::string_view topic, std::string_view filter) {
    std::string const tLower = toLower(topic);
    std::string const fLower = toLower(filter);
    bool const all = tLower.empty() || tLower == "all";

    auto root = matjson::makeObject({});

    if (all || tLower == "triggers") {
        auto arr = matjson::Value::array();
        for (auto const& t : triggerCatalog()) {
            if (!fLower.empty() && !containsCI(t.name, fLower) && !containsCI(t.category, fLower) &&
                !containsCI(t.description, fLower) && std::to_string(t.id) != fLower) {
                continue;
            }
            arr.push(matjson::makeObject({
                {"id", t.id},
                {"name", t.name},
                {"category", t.category},
                {"description", t.description},
                {"keys", t.params},
            }));
        }
        root["triggers"] = arr;
    }

    if (all || tLower == "properties") {
        auto arr = matjson::Value::array();
        for (auto const& p : propertyKeyCatalog()) {
            if (!fLower.empty() && !containsCI(p.name, fLower) && !containsCI(p.description, fLower) &&
                std::to_string(p.key) != fLower) {
                continue;
            }
            arr.push(matjson::makeObject({
                {"key", p.key},
                {"name", p.name},
                {"type", p.type},
                {"description", p.description},
            }));
        }
        root["objectProperties"] = arr;
    }

    if (all || tLower == "colors") {
        root["colorChannels"] = matjson::makeObject({
            {"customChannels", "1..999 (user-defined color channels)"},
            {"specialChannels", matjson::makeObject({
                {"1000", "BG (Background)"},
                {"1001", "G1 (Ground 1)"},
                {"1002", "Line (Ground Line)"},
                {"1003", "3DL (3D Line)"},
                {"1004", "Obj (Default Object)"},
                {"1005", "P1 (Player 1)"},
                {"1006", "P2 (Player 2)"},
                {"1007", "LBG (Light BG)"},
                {"1009", "G2 (Ground 2)"},
                {"1010", "Black"},
                {"1011", "White"},
                {"1012", "Lighter"},
            })},
            {"hsvFormat", "hSaSvSaddSaddV (e.g. '0a1a1a0a0' -> hueShift=-180..180, satMul=0..2, valMul=0..2, satAddFlag, valAddFlag)"},
        });
    }

    if (all || tLower == "zlayers") {
        root["zLayers"] = matjson::makeObject({
            {"B5", -5}, {"B4", -3}, {"B3", -1}, {"B2", 1}, {"B1", 3},
            {"Default", 0}, {"T1", 5}, {"T2", 7}, {"T3", 9}, {"T4", 11},
        });
    }

    if (all || tLower == "easings") {
        root["easings"] = matjson::makeObject({
            {"0", "None"}, {"1", "EaseInOut"}, {"2", "EaseIn"}, {"3", "EaseOut"},
            {"4", "ElasticInOut"}, {"5", "ElasticIn"}, {"6", "ElasticOut"},
            {"7", "BounceInOut"}, {"8", "BounceIn"}, {"9", "BounceOut"},
            {"10", "ExponentialInOut"}, {"11", "ExponentialIn"}, {"12", "ExponentialOut"},
            {"13", "SineInOut"}, {"14", "SineIn"}, {"15", "SineOut"},
            {"16", "BackInOut"}, {"17", "BackIn"}, {"18", "BackOut"},
        });
    }

    if (all || tLower == "gamemodes") {
        root["gamemodes"] = matjson::makeObject({
            {"0", "Cube"}, {"1", "Ship"}, {"2", "Ball"}, {"3", "UFO"},
            {"4", "Wave"}, {"5", "Robot"}, {"6", "Spider"}, {"7", "Swing"},
        });
        root["speeds"] = matjson::makeObject({
            {"0", "1x (Normal, ~311.58 u/s)"},
            {"1", "0.5x (Slow, ~251.16 u/s)"},
            {"2", "2x (Fast, ~387.42 u/s)"},
            {"3", "3x (Faster, ~468.0 u/s)"},
            {"4", "4x (Fastest, ~576.0 u/s)"},
        });
    }

    if (all || tLower == "editor_options" || tLower == "level_settings") {
        root["editorOptions"] = matjson::makeObject({
            {"camera", "Pan/zoom via set_editor_camera. Standard GD 1 block = 30x30 units. Ground line is Y=0; block row 1 center is Y=15."},
            {"showGrid", "LevelEditorLayer::m_showGrid - toggles block grid lines."},
            {"drawTriggerBoxes", "LevelEditorLayer::m_drawTriggerBoxes - shows trigger touch/hitbox outlines."},
            {"drawEffectLines", "LevelEditorLayer::m_drawEffectLines - shows trigger spawn/effect duration lines."},
            {"showDurationLines", "LevelEditorLayer::m_showDurationLines - shows duration bars after triggers."},
            {"showGround", "LevelEditorLayer::m_showGround - shows/hides ground in editor."},
            {"hideBackground", "LevelEditorLayer::m_hideBackground - hides background in editor."},
            {"previewMode", "LevelEditorLayer::m_previewMode - live-renders color/alpha/pulse/shader triggers at camera X."},
            {"previewParticles", "LevelEditorLayer::m_previewParticles - animates particle objects in editor."},
            {"previewAnimations", "LevelEditorLayer::m_previewAnimations - animates monster/object animations."},
            {"previewShaders", "LevelEditorLayer::m_previewShaders - previews 2.2 shaders in editor."},
            {"layerLockingEnabled", "LevelEditorLayer::m_layerLockingEnabled - locks inactive editor layers."},
            {"currentLayer", "LevelEditorLayer::m_currentLayer - active editor layer (-1 = All)."},
            {"modes", "EditorUI::m_selectedMode: 1=Delete, 2=Build, 3=Edit, 4=View."},
            {"swipeAndSnap", "EditorUI: m_swipeEnabled (drag-select/place), m_freeMoveEnabled, m_continuousSnap."},
        });
        root["levelSettings"] = matjson::makeObject({
            {"startMode", "0..7 (Cube..Swing)"},
            {"startSpeed", "0=Normal(1x), 1=Slow(0.5x), 2=Fast(2x), 3=Faster(3x), 4=Fastest(4x)"},
            {"startMini", "bool"}, {"startDual", "bool"}, {"twoPlayerMode", "bool"},
            {"platformerMode", "bool"}, {"mirrorMode", "bool"}, {"rotateGameplay", "bool"},
            {"reverseGameplay", "bool"}, {"backgroundIndex", "int (1..59)"}, {"groundIndex", "int (1..22)"},
            {"middleGroundIndex", "int (0..3)"}, {"groundLineIndex", "int (1..2)"},
            {"fontIndex", "int (0..59)"}, {"songOffset", "float (seconds)"},
            {"audioTrack", "int (official track 0..21)"}, {"songID", "int (Newgrounds/custom song ID)"},
            {"spawnGroup", "int"}, {"allowMultiRotation", "bool"}, {"enable22Changes", "bool"},
        });
    }

    if (all || tLower == "coordinates" || tLower == "decoration") {
        root["coordinatesAndDecoration"] = matjson::makeObject({
            {"unitScale", "1 standard block = 30 x 30 GD units. Standard 16:9 viewport height at zoom 1.0 = 320 GD units (~10.67 blocks) and width = 569 GD units (~19 blocks)."},
            {"groundLevel", "Y = 0 is the ground surface. A block sitting on the ground has center Y = 15. Row N (0-indexed) has center Y = 15 + N * 30."},
            {"snapSubgrid", "Half block = 15, Quarter = 7.5, Eighth = 3.75, 1/15th = 2, 1/30th = 1 unit."},
            {"decorationTips", jsonArray({
                "STRICT SCOPE: Only build what the user requests. When asked to decorate a structure, decorate ONLY the structure and its hazards - NEVER add unrequested backgrounds or extra scenery.",
                "NO SCATTERED OBJECTS: Every piece must have structural or artistic purpose. Avoid random floating particles, disconnected fragments, or orphaned shapes.",
                "COMPLETE & FLUSH ALIGNMENT: Query get_objects (worldY is used by default) or inspect_structure_bounds first. Keep all borders, slope hypotenuses (length = sqrt(dx^2+dy^2)), and inner fills flush with zero overhanging tips, missing edges, or stray internal seams across contiguous blocks.",
                "PRESERVE GAMEPLAY: Never move, delete, or alter original collision hitboxes unless asked. Set key 121=1 (noTouch) on all decorative objects.",
                "LAYERING & VISIBILITY: Use B5(-5)..B1(3) for depth behind structure, Default(0) for gameplay, T1(5)..T4(11) for foreground overlays. Place custom opaque covers on z_layer='T2' so default ID 1/slope sprites never bleed through.",
                "COLOR & LIGHTING DISCIPLINE: Allocate unused color channels and group IDs per structure. Enable blending=true only for dark/neon scenes; on bright/cream palettes use non-blending translucent colors so highlights do not wash out to white.",
                "COHERENT LEVEL DESIGN: Maintain consistent theme, clear hazard/spike readability, fair jump/orb spacing for the active speed, and clean sightlines.",
                "MACRO-STRUCTURED BLOCKS (MYTHIC STANDARD): Avoid repeating identical 30x30 cell motifs. Treat each connected platform/pillar as one continuous architectural silhouette (211, 694, 1759/1339, 1202) with two-tone bevel bands, stepped diagonal gradient strips (503/1009 + key 43 HSV shifts), or 3D skewing (warpY=131, warpX=132). Place corner star flares (1835/1837 + 1764) sparingly only on lit corners.",
                "ILLUSTRATED ART (CURVES + GRADIENTS): Build characters, hair, cloth folds, and detailed props by layering soft gradient shading (1009, 1838/1835, 1764, 1887) inside fine curved lineart from chained 507/508 segments at 0.12x-0.25x scale with tinted HSV outlines — never crude primitive-circle faces or raw 914 text.",
                "DEPTH HIERARCHY & ATMOSPHERE: Keep B5/B4 silhouettes desaturated and base-faded with clouds/mist (129/131 + 503/1888), B2/B1 midground muted without heavy outlines, and T1-T3 foreground crisp. On bright/pastel scenes keep blending=false with low-opacity translucency; on dark/neon scenes use blending=true rim lights + 2065 emitters (key 145, key 146=1) and 3608 Spawn Particle (51=particleGroup, 71=posGroup).",
                "MANDATORY ZOOMED QA: Always inspect with audit_level_quality + render_1to1 at mode='1to1' AND zoomed-in (zoom=1.8+, show_grid=true) at corners, slopes, and spikes. Fix any misaligned, clipped, or incomplete detail before finishing.",
            })},
        });
    }

    if (all || tLower == "geometry" || tLower == "cheatsheet") {
        root["geometryCheatsheet"] = matjson::makeObject({
            {"coordinateBridge", "ALL tools (place_object, place_objects_batch, place_trigger, get_objects, delete_objects, select_objects, transform_objects, set_editor_camera, render_1to1, inspect_structure_bounds, audit_level_quality, mirror_or_clone_structure) use worldY (ground = 0) by default. get_objects also returns internal y (=worldY+90); pass world_coords=false only to filter in internal y."},
            {"primitives", matjson::makeObject({
                {"211", "30x30 borderless square; scale_x/scale_y = width/height in blocks."},
                {"693", "30x30 borderless right triangle; 90-deg corner is bottom-right at rot=0. rot=-135 apex UP, +45 apex DOWN, +135 apex LEFT, -45 apex RIGHT. Keep scale_x==scale_y before non-90 rotations to avoid skew."},
                {"216", "30x30 symmetric isosceles triangle (apex UP at rot=0). Uses base_color=outline, detail_color=fill (has thin baked border)."},
                {"3637", "50x50 borderless solid circle at scale=1.0 (radius=25u). Use scale_x/scale_y for smooth ovals/crescents."},
                {"3636", "Thick colorable ring."},
                {"997_1000", "Ring arc segments: 997(~90deg), 998(~220deg), 999(~75deg), 1000(~45deg)."},
                {"503", "Linear gradient glow beam (avoid blending=true on bright/cream BGs)."},
            })},
            {"slopes", matjson::makeObject({
                {"1x1_45deg", "hypotenuse = 1.414 blocks (42.43u) at +-45 deg."},
                {"2x1_horizontal", "60x30 span: hypotenuse = 2.236 blocks (67.08u) at +-26.57 deg."},
                {"1x2_vertical", "30x60 span: hypotenuse = 2.236 blocks (67.08u) at +-63.43 deg."},
            })},
            {"cubePhysics", "1x speed = 311.58 u/s (~10.39 blocks/s); single cube jump ~65u high, ~135u span at 1x."},
        });
    }

    return root;
}

std::string buildReferenceMarkdown(std::string_view topic) {
    std::ostringstream os;
    std::string const tLower = toLower(topic);
    bool const all = tLower.empty() || tLower == "all";

    if (all || tLower == "coordinates" || tLower == "decoration") {
        os << "# Geometry Dash Editor MCP Reference\n\n"
           << "## Coordinate System & 1:1 Scale\n"
           << "- **1 Block** = `30 x 30` GD world units.\n"
           << "- **Ground surface**: `Y = 0`. A block resting on the ground has center `Y = 15`.\n"
           << "- **1:1 Viewport (zoom = 1.0)**: `320` GD units tall (~10.67 blocks) x `569` GD units wide (16:9).\n"
           << "- **Z-Layers**: `B5=-5`, `B4=-3`, `B3=-1`, `B2=1`, `B1=3`, `Default=0`, `T1=5`, `T2=7`, `T3=9`, `T4=11`.\n"
           << "- **Special Color Channels**: `1000=BG`, `1001=G1`, `1002=Line`, `1003=3DL`, `1004=Obj`, `1005=P1`, `1006=P2`, `1007=LBG`, `1009=G2`, `1010=Black`, `1011=White`, `1012=Lighter`.\n\n"
           << "## Quality, Scope & Alignment Rules\n"
           << "- **Strict Scope**: Build only what the user asks for. If decorating a structure, do not add unrequested backgrounds or external scenery.\n"
           << "- **Zero Scattered Objects**: Every object must connect purposefully to the structure or coherent level flow.\n"
           << "- **Complete & Flush Geometry**: Align all corners, borders, slopes, and fills precisely; avoid stray internal seam lines across adjacent blocks and overhanging border tips.\n"
           << "- **Macro-Structured Blocks (Mythic Standard)**: Build connected structures as unified beveled silhouettes (`211`, `694`, `1759`/`1339`, `1202`) with two-tone bevel bands, stepped diagonal shading (`503`/`1009` + key `43` HSV shifts), or 3D skewing (`warpY=131`, `warpX=132`), rather than repetitive per-cell squares. Add corner flares (`1835`/`1837` + `1764`) sparingly on lit corners only.\n"
           << "- **Illustrated Art & Depth Hierarchy**: Build character/organic art with chained thin `507`/`508` curved lineart (`0.12x–0.25x`) over layered soft shading (`1009`, `1838`, `1764`, `1887`). Separate `B5/B4` mist-faded silhouettes (`129`/`131` + `503`), muted `B2/B1` midground props, and crisp `T1–T3` foreground. Keep `blending=false` with low opacity on bright/pastel palettes; use `blending=true` + `2065` (`key 145`, `146=1`) / `3608` Spawn Particle on dark/neon scenes.\n"
           << "- **Visual QA**: Always inspect with `render_1to1` (`mode='1to1'` and zoomed `zoom=1.8+` with `show_grid=true`) on corners, slopes, and spikes before finishing.\n\n";
    }

    if (all || tLower == "geometry" || tLower == "cheatsheet") {
        os << "## Vector Geometry & Slope Cheatsheet\n"
           << "- **Unified World Y**: All MCP tools (`place_object`, `place_objects_batch`, `place_trigger`, `get_objects`, `delete_objects`, `select_objects`, `transform_objects`, `set_editor_camera`, `render_1to1`, `inspect_structure_bounds`, `audit_level_quality`, `mirror_or_clone_structure`) use `worldY` (ground = `0`) by default. `get_objects` returns both `worldY` and internal `y` (`= worldY + 90`).\n"
           << "- **ID `211`**: `30x30` borderless square (`scale_x`/`scale_y` = size in blocks).\n"
           << "- **ID `693`**: `30x30` borderless right triangle (`rot=0` puts 90-deg corner bottom-right; `-135` apex UP, `+45` DOWN, `+135` LEFT, `-45` RIGHT; keep `scale_x == scale_y` before non-90 rotations).\n"
           << "- **ID `216`**: `30x30` symmetric isosceles triangle (`base_color`=outline, `detail_color`=fill, thin baked border).\n"
           << "- **ID `3637`**: `50x50` borderless solid circle at `scale=1.0` (radius `25u`).\n"
           << "- **ID `997..1000`**: Ring arc segments (`997` ~90deg, `998` ~220deg, `999` ~75deg, `1000` ~45deg).\n"
           << "- **Slope Borders**: 1x1 (`45deg`) length = `1.414` blocks; 2x1 horizontal (`+-26.57deg`) & 1x2 vertical (`+-63.43deg`) length = `2.236` blocks.\n"
           << "- **Speeds**: `1x` = `311.58 u/s`, `0.5x` = `251.16 u/s`, `2x` = `387.42 u/s`, `3x` = `468.0 u/s`, `4x` = `576.0 u/s`.\n\n";
    }

    if (all || tLower == "triggers") {
        os << "## All Geometry Dash 2.2091 Triggers\n\n"
           << "| ID | Name | Category | Description | Key Properties |\n"
           << "|---:|------|----------|-------------|----------------|\n";
        for (auto const& t : triggerCatalog()) {
            os << "| " << t.id << " | " << t.name << " | " << t.category
               << " | " << t.description << " | `" << t.params << "` |\n";
        }
        os << "\n";
    }

    if (all || tLower == "properties") {
        os << "## Object & Trigger Property Keys\n\n"
           << "| Key | Name | Type | Description |\n"
           << "|---:|------|------|-------------|\n";
        for (auto const& p : propertyKeyCatalog()) {
            os << "| " << p.key << " | `" << p.name << "` | " << p.type
               << " | " << p.description << " |\n";
        }
        os << "\n";
    }

    if (all || tLower == "editor_options" || tLower == "level_settings") {
        os << "## Editor Options & Level Settings\n"
           << "- **Editor Toggles**: `showGrid`, `drawTriggerBoxes`, `drawEffectLines`, `showDurationLines`, `showGround`, `hideBackground`, `previewMode`, `previewParticles`, `previewAnimations`, `previewShaders`, `layerLockingEnabled`, `currentLayer` (`-1` = All).\n"
           << "- **Level Settings**: `startMode` (0..7), `startSpeed` (0..4), `startMini`, `startDual`, `twoPlayerMode`, `platformerMode`, `mirrorMode`, `rotateGameplay`, `reverseGameplay`, `backgroundIndex`, `groundIndex`, `middleGroundIndex`, `groundLineIndex`, `fontIndex`, `songOffset`, `audioTrack`, `songID`.\n";
    }

    return os.str();
}

std::string buildTriggerCatalogPromptBlock() {
    std::ostringstream os;
    os << "## Complete Trigger Specification & Save-String Codes (GD 2.2091)\n"
       << "Common trigger keys: 1=objectID, 2=x, 3=worldY (internal y = worldY+90), 10=duration(s), "
          "11=touchTriggered(0/1), 30=easing(0..18), 51=targetGroupID, 57=groups(dot-separated), "
          "62=spawnTriggered(0/1), 63=spawnDelay(s), 71=centerGroupID, 85=easingRate, "
          "87=multiTrigger(0/1), 115=ord, 116=channel, 135=hide(0/1), 535=useControlID(0/1).\n"
       << "Easing codes (key 30): 0=None, 1=EaseInOut, 2=EaseIn, 3=EaseOut, 4=ElasticInOut, "
          "5=ElasticIn, 6=ElasticOut, 7=BounceInOut, 8=BounceIn, 9=BounceOut, "
          "10=ExponentialInOut, 11=ExponentialIn, 12=ExponentialOut, "
          "13=SineInOut, 14=SineIn, 15=SineOut, 16=BackInOut, 17=BackIn, 18=BackOut.\n"
       << "Color channel IDs: 1..999=custom, 1000=BG, 1001=G1, 1002=Line, 1003=3DL, "
          "1004=Obj, 1005=P1, 1006=P2, 1007=LBG, 1009=G2, 1010=Black, 1011=White, 1012=Lighter.\n"
       << "place_trigger (and place_object/place_objects_batch with trigger+params) encoding:\n"
       << "- Top-level fields: trigger|object_id, x, y(worldY), duration->10, easing->30, "
          "easing_rate->85, target_group->51, center_group->71, touch_triggered->11, "
          "spawn_triggered->62, multi_trigger->87, groups->57.\n"
       << "- params.<field> -> save key: "
          "move_x->28, move_y->29, lock_player_x->58, lock_player_y->59, "
          "lock_camera_x->141, lock_camera_y->142, small_step->394, silent->144, "
          "use_target_move->100, target_mode->101, target_color->23, opacity->35, "
          "blending->17, player_color_1->15, player_color_2->16, copy_color_id->50, "
          "copy_opacity->60, copy_hsv->49, color('#RRGGBB'|{r,g,b})->7/8/9, r->7, g->8, b->9, "
          "fade_in->45, hold->46, fade_out->47, pulse_mode->48, pulse_target_type->52, "
          "main_only->65, detail_only->66, exclusive->86, activate_group->56, delay->63, "
          "delay_variance->556, remap->442, spawn_ordered->107, chance->10, stop_mode->580, "
          "degrees->68, times_360->69, lock_object_rotation->70, aim_target_id->105, "
          "follow_x_mod->72, follow_y_mod->73, shake_strength->75, shake_interval->76, "
          "animation_id->76, target_count->77, item_id->80, block_a->80, block_b->95, "
          "item_id2->95, comparison->88, zoom->371, exit_static->110, exit_instant->111, "
          "follow_object->212, follow_easing->213, edge_direction->164, timewarp->120, "
          "gravity_mod->148, scale_x->150, scale_y->151, song_channel->392, sound_id->392, "
          "song_id->404, volume->406, speed->405, pitch->407, start_ms->408, end_ms->410, "
          "loop->412, reverb->415, gradient_id->174, up_color->203, down_color->204, "
          "left_color->205, right_color->206, vertex_mode->207, blending_mode->208, "
          "gradient_layer->209, min_layer->175, max_layer->176, sequence->435, "
          "sequence_mode->437, chance_list->152, area_x->220, area_y->221, area_length->222, "
          "area_offset->223, area_easing->224, control_id->535, no_effects->444, "
          "no_sfx->445, instant->446, bpm->498, beats_per_bar->499.\n"
       << "- extra_props: {\"<numericKey>\": value} sets any GD object/trigger property key directly (including keys in object strings / raw GD saves).\n"
       << "All Triggers (id | name | category | spec | keys):\n";

    for (auto const& t : triggerCatalog()) {
        os << t.id << " | " << t.name << " | " << t.category << " | " << t.description;
        if (t.params && t.params[0] != '\0') {
            os << " | keys: " << t.params;
        }
        os << '\n';
    }
    return os.str();
}

matjson::Value searchObjectCatalog(
    std::string_view query,
    std::string_view category,
    int limit,
    bool queryGameToolbox
) {
    int const clampedLimit = std::clamp(limit, 1, 250);
    std::string const qLower = toLower(query);
    std::string const cLower = toLower(category);

    auto results = matjson::Value::array();
    std::unordered_set<int> seenIds;
    int count = 0;

    auto pushItem = [&](int id, std::string_view name, std::string_view cat, std::string_view notes, std::string_view frame) {
        if (count >= clampedLimit) return;
        if (!seenIds.insert(id).second) return;
        auto obj = matjson::makeObject({
            {"id", id},
            {"name", std::string(name)},
            {"category", std::string(cat)},
        });
        if (!notes.empty()) obj["notes"] = std::string(notes);
        if (!frame.empty()) obj["frame"] = std::string(frame);
        results.push(std::move(obj));
        ++count;
    };

    // Check curated objects first
    for (auto const& o : curatedObjectCatalog()) {
        if (!cLower.empty() && !containsCI(o.category, cLower)) continue;
        if (!qLower.empty() && !containsCI(o.name, qLower) && !containsCI(o.notes, qLower) &&
            !containsCI(o.category, qLower) && std::to_string(o.id) != qLower) {
            continue;
        }
        std::string frame;
#ifndef PAIM_EDITOR_MCP_HEADLESS
        if (queryGameToolbox) {
            if (auto* tb = ObjectToolbox::sharedState()) {
                if (char const* f = tb->intKeyToFrame(o.id)) frame = f;
            }
        }
#endif
        pushItem(o.id, o.name, o.category, o.notes, frame);
    }

    // Check curated triggers too if category matches or empty
    if (cLower.empty() || containsCI("Triggers", cLower)) {
        for (auto const& t : triggerCatalog()) {
            if (!qLower.empty() && !containsCI(t.name, qLower) && !containsCI(t.category, qLower) &&
                !containsCI(t.description, qLower) && std::to_string(t.id) != qLower) {
                continue;
            }
            std::string frame;
#ifndef PAIM_EDITOR_MCP_HEADLESS
            if (queryGameToolbox) {
                if (auto* tb = ObjectToolbox::sharedState()) {
                    if (char const* f = tb->intKeyToFrame(t.id)) frame = f;
                }
            }
#endif
            pushItem(t.id, t.name, std::string("Trigger: ") + t.category, t.description, frame);
        }
    }

#ifndef PAIM_EDITOR_MCP_HEADLESS
    // Query all 4000+ objects from GD's live ObjectToolbox!
    if (queryGameToolbox && count < clampedLimit && (cLower.empty() || cLower == "all" || cLower == "toolbox")) {
        if (auto* tb = ObjectToolbox::sharedState()) {
            for (auto const& [id, frame] : tb->m_allKeys) {
                if (count >= clampedLimit) break;
                if (!qLower.empty() && !containsCI(frame, qLower) && std::to_string(id) != qLower) {
                    continue;
                }
                pushItem(id, frame, "ObjectToolbox", "Live GD 2.2091 object frame", frame);
            }
        }
    }
#else
    (void)queryGameToolbox;
#endif

    return matjson::makeObject({
        {"count", count},
        {"limit", clampedLimit},
        {"items", results},
    });
}

} // namespace paimon::editormcp
