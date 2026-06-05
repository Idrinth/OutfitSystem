This tiny plugin for SKSE builds an outfit system that does not magically add items on outfit switch. This is done for my follower mod, but I thought it might be useful for others as well since it outperforms the papyrus implementation significantly.

Configuration files go into skse/plugins/IdrinthOutfitSystem/*.yml and are loaded automatically. While I use neither OSA nor Bath unequips, they should already be supported by detecting faction/magic effects and skipping the re-equip then.

Example Yaml for the screenshot:

```yaml
civilianLocations:
  - LocTypeCity
  - LocTypeInn
  - LocTypeTemple
npcs:
  - modName: IdrinthThalui.esp
    formId: 0x803
    outfits:
      - military:
          modName: IdrinthThalui.esp
          formId: 0xA7BF2
        civilian:
          modName: IdrinthThalui.esp
          formId: 0x4C5CDE
```

Of course you can add items that are not loadable everywhere, they will just be skipped.

Let me know if there are more cases to handle!
