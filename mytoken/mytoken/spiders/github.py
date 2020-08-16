import scrapy


class GithubSpider(scrapy.Spider):
    name = 'github'
    allowed_domains = ['github.com']
    start_urls = ['https://github.com/login']

    def parse(self, response):
        post_dict = {'login': 'zhang52linux','password': 'linux842563'}
        yield scrapy.FormRequest.from_response(response,callback=self.after_login,formdata=post_dict)


    def after_login(self,response):
        print(response.status)
        print(response.text)


