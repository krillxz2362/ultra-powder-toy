function love.conf(t)
    t.identity = "ultrapowdertoy"
    t.version = "11.5"
    t.console = false

    t.window.title = "ULTRA POWDER TOY"
    t.window.width = 480
    t.window.height = 854
    t.window.resizable = true
    t.window.vsync = 1
    t.window.msaa = 0
    t.window.highdpi = false

    t.modules.joystick = false
    t.modules.physics = false
    t.modules.video = false
end
