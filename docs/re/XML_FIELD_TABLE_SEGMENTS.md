# XML descriptor ownership

The retail descriptor table spans object classes. These rows identify tags whose position in the table assigns the wrong owner.

| Listed as | Really | Evidence |
|---|---|---|
| `MousePointer.Icon_Name` | `GameObjectType` / `Faction` / special-ability `Icon_Name` | MousePointers.xml writes no `Icon_Name`; all 1013 FoC uses are in object type, faction and ability elements |
| `MousePointer.Apply_To_All_Allies` | ability field (`Battlefield_Modifier`, `Enhance_Defense`, `Garrison_Upgrade`, `Weatherproof`, `Bounty` abilities) | FoC writes it only inside those ability elements |
| `HardwareConfiguration.Apply_To_All_Allies` | `Garrison_Upgrade_Ability` (and the abilities above) | GraphicDetails.xml writes none; the descriptor at +0x178 sits after the hardware fields in the same table run |
| `MousePointer.Affects_All_Allies` | ability field (`Enable_Radar_Ability`, `Enable_Ability`) | FoC writes it only inside those ability elements |
| `MousePointer.Ability_Name` | ability field (`Enable_Ability`, hero clash `Special_Ability_Action`) | FoC writes it only inside those elements |
| `HardwareConfiguration.Type` | ability `Type` (+0x18 of the ability record) | GraphicDetails.xml writes no `Type`; the 1606 FoC uses are ability / sub-object `Type` elements |
