# Unused retail XML fields

- `Camera_Stop_Left` and `Camera_Stop_Right` appear in the space-camera clamp at RVA 0x3c8d0. No caller or pointer to that function was found. `Camera_Z_Position` has no known reader.
- `Fleet_Maintenance_Update_Delay_Seconds` at offset 0x110 has no known reader outside the parser. The AI maintenance tokens read the maintenance cost separately.
- `Health_Bar_Scale`, `Land_Health_Bar_Scale`, and `Min_Health_Bar_Scale` have constructor defaults of 20, 20, and 0. The retail code sets those values without reading them for HUD layout.
- `Destroyed_Space_Tactical_Map` at planet type offset 0x6b0 appears in construction, destruction, and copying at RVAs 0x2c4370, 0x2c5b30, and 0x36c000. No gameplay reader was identified.
