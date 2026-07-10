# Min3D sprite map (Content/SpriteSrc/*.png, ids `<sheet>_<row>_<col>`)

Sheets: net (routes/structures), res (deposits/terrain), veh (vehicles/conveyors/pipes),
mac (machines/processing/power), ico (UI icons). Neon pink already keyed out; bilinear filtering.

## Game-role assignments
- **Miner**: net_05_02 (orange, with cart) / heavy: net_05_03 (blue)
- **Pump (water)**: net_08_04 (pump+pipe) or res_04_07 (fountain); pumpjack (oil): res_04_05
- **Depot/Storage**: net_04_03 (round silo, gem front), alt net_04_04, net_04_05; big hubs net_04_00..02
- **Smelter**: mac_00_00 (furnace w/ molten glow); alt mac_00_01..03
- **Foundry/steel**: mac_00_04 / mac_00_05 (coal furnace)
- **Water treatment**: mac_01_00 (blue drop machine), alt mac_01_02
- **Generator (power)**: mac_07_0 (yellow diesel gen); coal gen mac_07_3; solar mac_06_4; wind mac_06_5; battery mac_07_6
- **Lift**: net_02_04 (glass capsule) B/net_02_05 (blue arrow), net_02_06 (purple), net_02_07 (tall ladder shaft)
- **Plane connector**: veh_06_4 (gate w/ down arrow) & veh_06_6 (up blue); alt portal rings net_07_0..3
- **Transfer node**: veh_07_4 / veh_07_5 / veh_07_6
- **Junction marker**: net_03_0..7 (colored gem cross nodes; pick by cargo color)
- **Carts (loaded)**: veh_01_0 coal, veh_01_1 gold, veh_01_2 blue crystal, veh_01_3 purple, veh_01_4 green, veh_01_5 pink; empty veh_00_0..5; tanker water veh_01_6
- **Deposits (large mound)**: iron res_00_00, copper res_00_01, gold res_00_02, uranium res_00_03, coal res_00_04, oil res_00_05, water spring res_00_06, crystal res_00_07, quartz res_00_08
- **Deposit small piles**: res_03_x same order + salt res_03_05
- **Warning**: net_06_4 triangle, beacon net_06_5; icon warnings ico_04_2..7
- **Resource icons (HUD)**: oil ico_00_0, water ico_00_1, salt ico_00_2, gold-bar ico_00_3, iron-ingot ico_00_4, copper-ingot ico_00_5, uranium ico_00_6, coal ico_01_0, crystal ico_01_1, stone ico_01_2, sand ico_01_3, clay ico_01_4, gold-ore ico_01_5, power ico_01_6
- **Tool icons**: dig ico_03_0, tunnel ico_03_1, lift ico_03_2, transfer ico_03_3, junction ico_03_4, planes ico_03_6, miner ico_02_6 (drill), factory ico_02_4, cart ico_02_5, build ico_02_3, excavator ico_02_7, crate/depot ico_02_1
- **UI**: play ico_07_4, pause ico_07_5, ffwd ico_07_6, check ico_07_2, X ico_07_3, flag ico_07_1, target ico_07_0, matrix ico_09_0, axis ico_09_1, layerbox ico_09_2, lock ico_09_4, unlock ico_09_5, upgrade ico_06_0, stats ico_06_3, star ico_06_7, power ico_06_6
- **Plane chips**: ico_08_0..5 (hex chips), pins ico_08_6..10
- **Effects**: sparkle veh_09_5, ring veh_09_6, beam veh_09_7, cargo gems veh_09_8..12
- Concept mockups for reference: Images/*01_11_49*.png (plane view), *01_11_53*.png (full UI), *01_11_57*.png (matrix view)
