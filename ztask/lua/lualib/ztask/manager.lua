--服务管理
local ztask = require "ztask"
local c = require "ztask.core"

function ztask.launch(...)
	local addr = c.command("LAUNCH", table.concat({...}," "))
	if addr then
		return tonumber("0x" .. string.sub(addr , 2))
	end
end

function ztask.kill(name)
	if type(name) == "number" then
		ztask.send(".launcher","lua","REMOVE",name, true)
		name = ztask.address(name)
	end
	c.command("KILL",name)
end

function ztask.abort()
	c.command("ABORT")
end

local function globalname(name, handle)
	local c = string.sub(name,1,1)
	assert(c ~= ':')
	if c == '.' then
		return false
	end

	assert(#name <= 16)	-- GLOBALNAME_LENGTH is 16, defined in ztask_harbor.h
	assert(tonumber(name) == nil)	-- global name can't be number

	local harbor = require "ztask.harbor"

	harbor.globalname(name, handle)

	return true
end

function ztask.register(name)
	if not globalname(name) then
		c.command("REG", name)
	end
end

function ztask.alias(name)
	c.command("ALIAS", name)
end

function ztask.name(name, handle)
	if not globalname(name, handle) then
		c.command("NAME", name .. " " .. ztask.address(handle))
	end
end

local dispatch_message = ztask.dispatch_message

function ztask.forward_type(map, start_func)
	c.callback(function(ptype, msg, sz, ...)
		local prototype = map[ptype]
		if prototype then
			dispatch_message(prototype, msg, sz, ...)
		else
			dispatch_message(ptype, msg, sz, ...)
			c.trash(msg, sz)
		end
	end, true)
	ztask.timeout(0, function()
		ztask.init_service(start_func)
	end)
end

function ztask.filter(f ,start_func)
	c.callback(function(...)
		dispatch_message(f(...))
	end)
	ztask.timeout(0, function()
		ztask.init_service(start_func)
	end)
end

function ztask.monitor(service, query)
	local monitor
	if query then
		monitor = ztask.queryservice(true, service)
	else
		monitor = ztask.uniqueservice(true, service)
	end
	assert(monitor, "Monitor launch failed")
	c.command("MONITOR", string.format(":%08x", monitor))
	return monitor
end

return ztask
