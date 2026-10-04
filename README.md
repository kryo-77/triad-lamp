# Triad Lamp

Inspired by a delta robot's structure, and movement (blades flapping) from Odradek from Death Stranding.

 !!(!WORK IN PROGRESS)!!!
<img width="1920" height="1080" alt="triad_corner" src="https://github.com/user-attachments/assets/e84af2c4-64db-46e6-b02a-59ef9b237a48" />
<img width="1920" height="1080" alt="triad_front" src="https://github.com/user-attachments/assets/1938c62d-480f-4875-ad92-7f5055086176" />
<img width="1920" height="1080" alt="triad_back" src="https://github.com/user-attachments/assets/8c2af4a3-9fd4-4f12-a1cb-75cc3775fb7e" />

## Updates:
- Oct 4: Added hand detection which can control LEDs only. Added code files, all 3d print files (f3d format).
- Oct 3: Created this repo. Added README, combined stl (not for printing). Wrote code for controlling leds, servo, website control.

## To-do list:
- add wiring diagram to repo
- make changes to mount

## Components used
- (1x) ESP32 DevKit V1
- (6x) 12V LEDs
- (3x) Transistors (BC547)
- (1x) 12V battery pack
- (1x) Step-down converter (12V to 6V for the servos)
- (3x) MG90 servos

## 3D printing
- `triad-lamp-assembly.stl`: the full combined model of the lamp.
-  Actual files (used in Fusion 360) available in the `f3d` folder.
  
### Current print settings:
-   Blades (led bars), triangle: Black PETG, 20% infill
-   LED diffuser: White PLA
-   L bracket & servo holder: Black PETG, 40% infill

## Dimensions
- **Triangle:** 14 cm top edge, 10 cm sides, 3 cm thick
- **Blade:** 15 x 2.5 x 1.5 cm (19 cm total with the servo mount)


## IRL demo: 

<img width="800" height="450" alt="ezgif com-video-to-gif-converter" src="https://github.com/user-attachments/assets/71ba9c96-879c-4597-b8d6-0247b53aa574" />
<img width="800" height="450" alt="servo_move2" src="https://github.com/user-attachments/assets/ee3655d2-9a5c-4a2a-afa4-ec69be214768" />

## Open-cv hand detection demo (LEDs only):
<img width="800" height="450" alt="handdec2" src="https://github.com/user-attachments/assets/7911b6ee-f255-4b6e-a609-32a185fa8eb9" />

