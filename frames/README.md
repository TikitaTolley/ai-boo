# Frames

`tools/capture.sh` saves camera frames here, one folder per type of shot. They stay on your machine and are never committed.

```
frames/
├── training/<type>/   everything captured, e.g. training/hallway-empty/ or training/walk-past-1/
└── content/<type>/    copies of sessions you pick for videos
```

Name each type after what is in it. Folders of empty scenes (`...-empty`) are already labelled "no person", so only walk-past sessions need labelling frame by frame.

Run `tools/capture.sh list` to see every type and how many frames it holds.
