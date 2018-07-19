local ztask = require "ztask"

local clusterd
local cluster = {}

function cluster.call(node, address, ...)
	-- ztask.pack(...) will free by cluster.core.packrequest
	return ztask.call(clusterd, "lua", "req", node, address, ztask.pack(...))
end

function cluster.send(node, address, ...)
	-- push is the same with req, but no response
	ztask.send(clusterd, "lua", "push", node, address, ztask.pack(...))
end

function cluster.open(port)
	if type(port) == "string" then
		ztask.call(clusterd, "lua", "listen", port)
	else
		ztask.call(clusterd, "lua", "listen", "0.0.0.0", port)
	end
end

function cluster.reload(config)
	ztask.call(clusterd, "lua", "reload", config)
end

function cluster.proxy(node, name)
	return ztask.call(clusterd, "lua", "proxy", node, name)
end

function cluster.snax(node, name, address)
	local snax = require "snax"
	if not address then
		address = cluster.call(node, ".service", "QUERY", "snaxd" , name)
	end
	local handle = ztask.call(clusterd, "lua", "proxy", node, address)
	return snax.bind(handle, name)
end

function cluster.register(name, addr)
	assert(type(name) == "string")
	assert(addr == nil or type(addr) == "number")
	return ztask.call(clusterd, "lua", "register", name, addr)
end

function cluster.query(node, name)
	return ztask.call(clusterd, "lua", "req", node, 0, ztask.pack(name))
end

ztask.init(function()
	clusterd = ztask.uniqueservice("clusterd")
end)

return cluster
