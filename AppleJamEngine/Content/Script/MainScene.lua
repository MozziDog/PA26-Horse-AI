local audio_loaded = false

local function load_audio()
    if audio_loaded then
        return
    end

    audio_loaded = true
    Audio.Load("Start", "Start.wav", false)
end

function BeginPlay()
    load_audio()
    Audio.PlayBGM("Start", 0.7)
end

function EndPlay()
    Audio.StopBGM()
end

function OnOverlap(OtherActor)
end

function Tick(dt)
end
