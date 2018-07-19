local ztask = require "ztask"

local harbor = {}

function harbor.globalname(name, handle)
	handle = handle or ztask.self()
	ztask.send(".cslave", "lua", "REGISTER", name, handle)
end

function harbor.queryname(name)
	return ztask.call(".cslave", "lua", "QUERYNAME", name)
end
--监控节点
function harbor.link(id)
	ztask.call(".cslave", "lua", "LINK", id)
end

function harbor.connect(id)
	ztask.call(".cslave", "lua", "CONNECT", id)
end
--监控主节点
function harbor.linkmaster()
	ztask.call(".cslave", "lua", "LINKMASTER")
end

return harbor
