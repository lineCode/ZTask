local ztask = require "ztask"
local debugchannel = require "ztask.debugchannel"

local CMD = {}

local channel

function CMD.start(address, fd)
	assert(channel == nil, "start more than once")
	ztask.error(string.format("Attach to :%08x", address))
	local handle
	channel, handle = debugchannel.create()
	local ok, err = pcall(ztask.call, address, "debug", "REMOTEDEBUG", fd, handle)
	if not ok then
		ztask.ret(ztask.pack(false, "Debugger attach failed"))
	else
		-- todo hook
		ztask.ret(ztask.pack(true))
	end
	ztask.exit()
end

function CMD.cmd(cmdline)
	channel:write(cmdline)
end

function CMD.ping()
	ztask.ret()
end

ztask.start(function()
	ztask.dispatch("lua", function(_,_,cmd,...)
		local f = CMD[cmd]
		f(...)
	end)
end)
