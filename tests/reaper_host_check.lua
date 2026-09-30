-- Optional host acceptance harness. Run in an isolated REAPER instance/config:
-- reaper -newinst -cfgfile <build>/host-check/reaper.ini <this script>
-- Generate state.bin/sample.wav first: plugin_tests <absolute fixture directory>.
-- The fixture directory is the isolated REAPER resource directory.
local dir = reaper.GetResourcePath():gsub('\\', '/')
local function write(name, text)
  local f = assert(io.open(dir .. '/' .. name, 'w')); f:write(text); f:close()
end
reaper.InsertTrackAtIndex(0, true)
local track = reaper.GetTrack(0, 0)
reaper.GetSetMediaTrackInfo_String(track, 'P_NAME', 'Sineweave acceptance', true)
local fx = reaper.TrackFX_AddByName(track, 'Sineweave', false, -1)
if fx < 0 then write('host-result.txt', 'FAIL: VST3 could not be loaded'); return end
local params = {}
for i = 0, math.min(23, reaper.TrackFX_GetNumParams(track, fx) - 1) do
  local _, name = reaper.TrackFX_GetParamName(track, fx, i)
  params[#params + 1] = tostring(i) .. ': ' .. name
end
write('host-parameters.txt', table.concat(params, '\n'))
local trajectory = io.open(dir .. '/trajectory-mode.txt', 'r')
if trajectory then trajectory:close() end
local f = assert(io.open(dir .. (trajectory and '/trajectory.base64' or '/state.base64'), 'r'))
local chunk = f:read('*a'); f:close()
local accepted = reaper.TrackFX_SetNamedConfigParm(track, fx, 'vst_chunk', chunk)
if not accepted then write('host-result.txt', 'FAIL: VST3 state not accepted'); return end
-- Allow asynchronous state preparation before saving or starting playback.
local started = reaper.time_precise()
local function finish()
  if reaper.time_precise() - started < 1 then reaper.defer(finish); return end
  local item = reaper.CreateNewMIDIItemInProj(track, 0, 4, false)
  local take = reaper.GetActiveTake(item)
  for _, n in ipairs({{0.1, 1.5, 69}, {2.0, 3.4, 81}}) do
    reaper.MIDI_InsertNote(take, false, false,
      reaper.MIDI_GetPPQPosFromProjTime(take, n[1]),
      reaper.MIDI_GetPPQPosFromProjTime(take, n[2]), 0, n[3], 100, true)
  end
  reaper.MIDI_Sort(take)
  reaper.GetSetProjectInfo(0, 'RENDER_BOUNDSFLAG', 0, true)
  reaper.GetSetProjectInfo(0, 'RENDER_STARTPOS', 0, true)
  reaper.GetSetProjectInfo(0, 'RENDER_ENDPOS', 4, true)
  reaper.GetSetProjectInfo(0, 'RENDER_SETTINGS', 0, true)
  reaper.GetSetProjectInfo(0, 'RENDER_SRATE', 48000, true)
  reaper.GetSetProjectInfo(0, 'RENDER_CHANNELS', 2, true)
  reaper.GetSetProjectInfo(0, 'RENDER_TAILFLAG', 0, true)
  reaper.GetSetProjectInfo_String(0, 'RENDER_FILE', dir, true)
  reaper.GetSetProjectInfo_String(0, 'RENDER_PATTERN', 'host-output', true)
  reaper.GetSetProjectInfo_String(0, 'RENDER_FORMAT', 'ZXZhdxgAAA==', true)
  reaper.Main_SaveProjectEx(0, dir .. '/acceptance.rpp', 0)
  reaper.TrackFX_Show(track, fx, 3)
  write('host-result.txt', 'PASS: VST3 loaded, state submitted, MIDI project saved; verify rendered output separately')
end
reaper.defer(finish)
