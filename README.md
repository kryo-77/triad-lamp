# Triad Lamp

Inspired by a delta robot's structure, and movement (blades flapping) from Odradek from Death Stranding.

 !!!(WORK IN PROGRESS)!!!
<img width="1920" height="1080" alt="triad_corner" src="https://github.com/user-attachments/assets/e84af2c4-64db-46e6-b02a-59ef9b237a48" />
<img width="1920" height="1080" alt="triad_front" src="https://github.com/user-attachments/assets/1938c62d-480f-4875-ad92-7f5055086176" />
<img width="1920" height="1080" alt="triad_back" src="https://github.com/user-attachments/assets/8c2af4a3-9fd4-4f12-a1cb-75cc3775fb7e" />

## Updates:
- Oct 4: Added hand detection which can control LEDs only. Added code files, all 3d print files (f3d format).
- Oct 3: Created this repo. Added README, combined stl (not for printing). Wrote code for controlling leds, servo, website control.
- Oct 5: wiring diagram to repo

## To-do list:
- make hand detection work with servos as well
- make changes to mount (cad)

# Bill of Materials

| # | Component | Qty |
|---|-----------|-----|
| 1 | ESP32 DevKit V1 | 1 | 
| 2 | 12V LEDs | 6 | 
| 3 | Step-down converter (12V to 5V) | 1 |
| 4 | Transistors | 3 |
| 5 | 12V battery, 1500mAh | 1 | 
| 6 | MG90 servo | 3 | 

- Each transistor drives a group of 2 LEDs (6 LEDs / 3 transistors).


## Wiring diagram (servos ARE switched)
<img width="1078" height="798" alt="{A9B57E3E-D54B-4706-AFF7-D16A3712C8A2}" src="https://github.com/user-attachments/assets/e42e3653-ef19-4d57-9651-e6997432cd83" />


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


## How to assemble parts :
### **Step 1**: 

Take out all printed parts and group identical ones together (it follows the order you attach the parts too). You will have: 
- `1x triangle face plate`
- `1x triangle body`
- `3x L- brackets`
- `3x servo mounts` (A servo mount has 2 parts: `base` & `U shaped horn` so technically 6 parts)
- `3x blades`
- `3x LED diffusers` 

For attaching the parts together:
- `6x M3 screws + nuts`
- `Super glue`

### **Step 2**: 

Take the 6x M3 screws and nuts, and attach the `L bracket` to the `triangle body`. The triangle body must be pointing its hollow side towards you, and the L brackets should also have the bent part pointing towards you. It only requires 6 screws to attach the 3 brackets to the body. You can use super glue if you want but its not needed here. 

<img width="523" height="307" alt="{A08F8EFB-94DE-4AD3-80D5-EE5F760460B0}" src="https://github.com/user-attachments/assets/1c72ba9b-9e2b-4b02-8ce3-d6910a7d632c" />


### **Step 3**: 

Once the triangle and L brackets are assembled, you need to attach the servo mount `base` so that the flat side faces inwards (kind of). 

Before attaching the servo base to the L-bracket, you need to first attach the MG90 servo to the base. No need to screw those in, just push them inside.

After servos are inside the servo base, attach it to L bracket. You will need to use super glue. (I downloaded the servo mount online instead of making my own and forgot to update the screw holes)

It doesn't matter if the flat side for servo base faces inward or outwards, but the flat side inward just looks cleaner.

Dont use glue after servos are pressed into the servo base as you might need to remove them again (I shorted 3 of them lol).

<img width="714" height="591" alt="{1D76E013-7360-4A4D-83A5-6958B6ABBFB9}" src="https://github.com/user-attachments/assets/7be1747d-70e1-497c-a414-0c8361bdff3a" />

### **Step 4**: 

After the `triangle base`,  `L brackets` and `servo base` are attached together, you need to first make the 3x servos have their angle be 90 degrees before attaching the horn & blades together. Make sure the servo angle is 90 degrees otherwise there's a chance the servos could break something.

### **Step 5**: 

Once servo angles are 90 degrees, you can attach the `U shaped servo horn` (3d printed). Once that's on the servo, use the normal plastic servo horns that come with the servos and push them inside the U shape bracket (there is a servo horn hole inside it). No need for screws or glue in this. Now test the servos and make sure they look like the image: 

<img width="661" height="441" alt="{DE57047D-020A-4B16-875F-915852AFC5CE}" src="https://github.com/user-attachments/assets/af2cf50a-7939-4011-8794-9e7ef93ed2c7" />

### **Step 6**: 

Once all that is done, we need to attach the `blades` to the `U shaped servo horn`. You will need to use glue for this, not screws. 

Attach the small rectangular piece sticking out under the blade to the U shaped horn using glue. It should look like a phone stand (U horn being the stand and blade being the phone).

<img width="354" height="301" alt="{E9A76CCE-B7AE-441C-954C-DCD5754E55B9}" src="https://github.com/user-attachments/assets/7c3f867a-a9e9-4a19-9a8e-e5d16635bb19" />


### **Step 7**: 

Add your 12V LEDs inside the `blade`. There are 2 LEDs per blade so first cut and solder the wires and test the LEDs. Make sure to attach long (~60cm) wires at the last LED (red/black color for +/-).

You can either use glue to attach the LEDs inside the blade but I prefer double sided tape, both work but double sided tape just means easy swapping out the LEDs if something goes wrong. 

There is a hole near the bottom of blade where you can insert the wires.

### **Step 8 - Final step**:

After the LEDs are attached, you can finally attach the `LED diffuser` to the blade. Use white glue on the walls of the `blade`. I used glue as screw holes wouldn't look good because the blades are thin and long.

Finally your Triad-Lamp should look like this!:

<img width="945" height="554" alt="{FA95A0F9-2197-46DC-8254-415247F50475}" src="https://github.com/user-attachments/assets/7e82acd3-a5d4-4aa6-982b-6c239d4ece51" />

(follow `wiring diagram` on top after this assembly)

## IRL demo: 

<img width="800" height="450" alt="ezgif com-video-to-gif-converter" src="https://github.com/user-attachments/assets/71ba9c96-879c-4597-b8d6-0247b53aa574" />
<img width="800" height="450" alt="servo_move2" src="https://github.com/user-attachments/assets/ee3655d2-9a5c-4a2a-afa4-ec69be214768" />

## Open-cv hand detection demo (LEDs only):
<img width="800" height="450" alt="handdec2" src="https://github.com/user-attachments/assets/7911b6ee-f255-4b6e-a609-32a185fa8eb9" />

