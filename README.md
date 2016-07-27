# Oring

Oring is a work title for a from-scratch Wayland application whose purpose has
changed over time.

Currently `oring-cal`, poorly named as it was meant to be something else, is
aiming to become an example of how to write a Wayland application, that:
- [x] Has a proper main event loop using the thread-safe Wayland client API.
- [x] Reacts to wl_seat and wl_output removals.
- [x] Predicts accurately the time when the output frame will be displayed.
- [x] Falls back to guessing if prediction is not available.
- [x] Rendering is event-triggered, not a CPU&GPU hog like with stupid games.
- [ ] Supports triggering rendering from frame callback if more rendering
time is needed.
- [x] Uses EGL and GL for the rendering.
- [ ] Uses big OpenGL (apparently the myth of GL ES only might still live).
- [ ] GL rendering happens in a thread outside of the main thread.
- [ ] Has a trivial physical model controlling the rendered scene.
- [ ] The physical model uses the predicted presentation timestamp for
the rendered scene; use a predicted scene state.
- [ ] Input affects the physical simulation.
- [ ] Handles input based on the timestamps, not by the moment of receive.
- [ ] Predicts pointer motion for the predicted presentation time when
pointer is controlling the physical model.
- [ ] Draws statistics of frame timings.
- [ ] The scene is complicated enough that rendering actually takes a bit of
time, just for the sake of emulating something more than a glxgears-level of
work load.
- [ ] Accommodates when rendering rate cannot keep up with the display.

Obviously, there is a long to way to go.

