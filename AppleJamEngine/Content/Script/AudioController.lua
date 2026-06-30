local audio_loaded = false

local function load_audio()
    if audio_loaded then
        return
    end

    audio_loaded = true
    Audio.Load("Start", "Start.wav", false)
    Audio.Load("DragonBoss", "DragonBoss.wav", true)
    Audio.Load("Arrow", "Arrow.wav", false)
    Audio.Load("Fire", "Fire.wav", false)
    Audio.Load("Hit", "Hit.wav", false)
    Audio.Load("Jump", "Jump.wav", false)
    Audio.Load("Land", "Land.wav", false)
    Audio.Load("Walking", "Walking.wav", true)
    Audio.Load("CutScene", "CutScene.wav", false)
    Audio.Load("Dash", "Dash.wav", false)
    Audio.Load("Bowcharging", "Bowcharging.mp3", false)
    Audio.Load("Explosion", "Explosion.wav", false)

    Audio.Load("DragonGrowl", "DragonGrowl.mp3", false)
    Audio.Load("DragonGrowl2", "DragonGrowl2.mp3", false)
    Audio.Load("DragonGrowl3", "DragonGrowl3.mp3", false)
    Audio.Load("DragonDeath", "DragonDeath.mp3", false)
end

function BeginPlay()
    load_audio()
    Audio.PlayBGM("DragonBoss", 0.7)
end

function EndPlay()
    Audio.StopBGM()
    Audio.StopLoop("HaruWalking")
end

function OnOverlap(OtherActor)
end

function Tick(dt)
end
