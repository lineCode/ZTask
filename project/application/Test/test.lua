local ztask = require "ztask"
require "ztask.manager"
local cURL = require "curl"

ztask.info_func(function()
ztask.error("ddff")
return "sdfs","fsfdsdf"

end)
local a=0
--È¤¶àÅÄ²É¼¯
function getlist()
	local c = cURL.easy_init()
	while(true)
	do
		c:setopt_url("http://wwwapi.quduopai.cn/www/video/discoverList?dtu=300&page=1&page_size=20")
		cURL.send(c)
		--a=a+1
		--print(a.."\r\n")
		--c=nil
	end
end

ztask.start(function()
	for i=0,40,1
	do
		ztask.fork(getlist)
	end
end)
