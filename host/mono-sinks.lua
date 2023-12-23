-- Define the nodes table with initial state
local nodes = {
    mono_sink_left = nil,
    mono_sink_right = nil,
    adapter_found = false,
    adapter_name = "alsa_output.usb-C-Media_Electronics_Inc._USB_Audio_Device-00.analog-stereo",
}

-- Object Managers for nodes and SiLinkable objects
node_om = ObjectManager {
    Interest {
        type = "node",
        Constraint { "media.class", "matches", "Audio/Sink", type = "pw-global" },
    }
}

silinks_om = ObjectManager {
    Interest {
        type = "SiLinkable",
    }
}

-- Function to create a mono sink
local function createMonoSink(name, description, position)
    if nodes[name] then
        Log.warn("Mono Sink '" .. description .. "' already exists.")
        return
    end

    Log.info("Creating Mono Sink: " .. description)

    local properties = {
        ["node.name"] = name,
        ["node.description"] = description,
        ["audio.rate"] = 48000,
        ["audio.channels"] = 1,
        ["audio.position"] = position,
        ["media.class"] = "Audio/Sink",
        ["factory.name"] = "support.null-audio-sink",
        ["node.virtual"] = "true",
    }

    nodes[name] = LocalNode("adapter", properties)
    nodes[name]:activate(Feature.Proxy.BOUND)

    Log.info("Mono Sink '" .. description .. "' activated: " .. properties["node.name"])
end

-- Function to link ports between nodes
local function linkPorts(output_node, output_port, input_node, input_port)
    if not output_node or not input_node then
        Log.error("Invalid nodes specified for linking.")
        return
    end

    local args = {
        ["link.output.node"] = output_node,
        ["link.output.port"] = output_port,
        ["link.input.node"] = input_node,
        ["link.input.port"] = input_port,
        ["object.id"] = nil,
        ["object.linger"] = true,
    }

    local link = Link("link-factory", args)
    link:activate(1)

    Log.info("Linked ports: " .. output_node .. ":" .. output_port .. " -> " .. input_node .. ":" .. input_port)
end

-- Rescan and link channels when conditions are met
local function rescan()
    if nodes.mono_sink_left and nodes.adapter_found then
        Log.info("Linking left channel")
        linkPorts("mono_sink_left", "monitor_FL", nodes.adapter_name, "playback_FL")
    end

    if nodes.mono_sink_right and nodes.adapter_found then
        Log.info("Linking right channel")
        linkPorts("mono_sink_right", "monitor_FR", nodes.adapter_name, "playback_FR")
    end
end

-- Handle object addition
local function onObjectAdded(_, node)
    local node_name = node.properties["node.name"] or "unknown"
    Log.info("Object added: " .. node.properties["object.id"] .. " (" .. node_name .. ")")

    if node_name == nodes.adapter_name then
        nodes.adapter_found = true
        rescan()
    end

    if not nodes.mono_sink_left then
        createMonoSink("mono_sink_left", "Mono Sink Left", "FL")
    end

    if not nodes.mono_sink_right then
        createMonoSink("mono_sink_right", "Mono Sink Right", "FR")
    end
end

-- Handle object removal
local function onObjectRemoved(_, node)
    local node_name = node.properties["node.name"] or "unknown"
    Log.info("Object removed: " .. node.properties["object.id"] .. " (" .. node_name .. ")")

    if node_name == "mono_sink_right" then
        nodes.mono_sink_right = nil
    elseif node_name == "mono_sink_left" then
        nodes.mono_sink_left = nil
    elseif node_name == nodes.adapter_name then
        nodes.adapter_found = false
    end
end

-- Connect events to object managers
node_om:connect("object-added", onObjectAdded)
node_om:connect("object-removed", onObjectRemoved)
silinks_om:connect("objects-changed", rescan)

-- Activate object managers
node_om:activate()
silinks_om:activate()

Log.info("Script initialized successfully.")

