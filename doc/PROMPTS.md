Let's build a test env for the web based UI for this thing. I want to
be able to run the web server on Linux host here to serve the pages as
we develop them. This would then control mock hardware on the host for
the test env. This would all be done with the target ESP32 also in
mind so we can serve up the resulting debugged UI from ESP32 when
we're stable enough to try that. I don't want to mock the WSPR
encoding process, but the "OK transmit this now" API needs to exist in
mock so the scheduler and web UI to control it can be run in the test
env. Config like call sign, grid square (specified by user or obtained
via GNSS), time of day (specified by user, obtained via SNTP, or
obtained via GNSS), timezone (user, or via grid square or GNSS
location), schedule for each band to transmit.


Let's add file transfer to and from target filesystem ReST API
endpoints. This would be for retrieval and/or changes to "static files
served from filesystem" to do quick testing of an idea or
saving/restoring config (which would be in file system as something
like JSON). I don't want logs and stats stored in file system because
wear on the flash could be a concern, so they need to be separate from
file system. Let's mock these additional ReST endpoints as well so
they can be tested and employed in the mock env as well as on the
target for test scripts.


# NOTES:

* TODO:
  * GNSS PPS based calibration for TCXO.
  * Firmware maintenance via web.
  * Firmware maintenance via console?
  * File transfer to/from target via web and/or console.
