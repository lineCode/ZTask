local ztask = require "ztask"
local curl = require "curl.core"


function curl.send(c)
	local session = ztask.genid()
	--提交请求
	c:perform(ztask.self(),session)
	--等待返回
	local msg,sz=ztask.yield(session)
	--处理数据
	return c:data(msg,sz)
end

return curl
