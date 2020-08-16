# -*- coding: utf-8 -*-
import scrapy
from words.items import WordsItem
from words.pipelines import WordsPipeline
import re


class MywordsSpider(scrapy.Spider):
    name = 'mywords'
    allowed_domains = ['chazidian.com']
    start_urls = ['https://chazidian.com/']
    db = WordsPipeline()
    reg = re.compile('https://www.chazidian.com/zi_\w+_\d+/')

    def parse(self, response):
        pinyin_set = response.xpath('//*[@id="danzi"]/div[1]/div/div[2]/div[2]/ul/li/a/@href').getall()
        for pinyin in pinyin_set:
            yield response.follow(pinyin,callback=self.collect_pinyin_2)
            # yield scrapy.Request(pinyin,callback=self.collect_pinyin_2)
            # if not len(response.xpath("//a[@class='negative' and id='next']"))
            #      response.xpath("//a[id='next']/@href")

    def collect_pinyin_2(self,response):
        pinyin_set = response.xpath('.//div[@class="pyjs_c"]/div/a/@href').getall()
        for pinyin in pinyin_set:
            item = re.findall(self.reg,pinyin)
            if len(item) != 0:
                yield response.follow(item[0], callback=self.collect_pinyin_3)

    def collect_pinyin_3(self, response):
        pinyin_set = response.xpath('//div[@class="box_content"]/div/a/@href').getall()
        pinyin_name = response.xpath('//div[@class="box_content"]/div/a/@title').getall()
        for pinyin, name in zip(pinyin_set, pinyin_name):
            cur_res = self.db.search_db(name)  # mysql去重操作
            if cur_res == 0:
                yield response.follow(pinyin, callback=self.collect_pinyin_4,meta={'name':name})

    def collect_pinyin_4(self, response):
        name = response.meta['name']
        print("******************************{}******************************".format(name))
        self.pinyin = ""  # 拼音
        self.bihua = ""  # 笔画
        self.jiben = ""  # 基本解释
        self.xiangxi1 = ""  # 详细解释
        self.xiangxi2 = ""  # 词性变化
        self.xiangxi3 = ""  # 同音字
        self.hanyu = ""  # 汉语字典
        self.num = 0  # 做信号
        self.bushou = "" #简体部首
        # 获取笔画、拼音
        try:
            self.pinyin = response.xpath('//div[@class="list_tico"]/ul/li[1]/text()').extract_first()
            self.bihua = response.xpath('//div[@class="list_tico"]/ul/li[4]/text()').extract_first()
            self.bushou = response.xpath('//div[@class="list_tico"]/ul/li[7]/text()').extract_first()
        except Exception as e:
            print(e)
        print("拼音为:{}".format(self.pinyin))
        print("总笔画数:{}".format(self.bihua))
        print("简体部首:{}".format(self.bushou))
        # 获取基本信息
        try:
            title = response.xpath('//div[@class="cont_hzcx"]/div[@class="zi_text_content"]/h2[1]/text()').getall()
            title_temp = response.xpath('//div[@class="nav_b"]/ul/li//text()').getall()
            if title[0] == '基本解释':
                word_data_list = response.xpath('//div[@class="cont_hzcx"]/div[@class="zi_text_content"]/p[1]//text()').getall()
                for item in word_data_list:
                    self.jiben += item
                self.jiben = str(self.jiben.replace('\\r\\n', '')).replace('\'', '').replace('“', '"').replace('”','"').replace('（', '(').replace('）', ')')
                print(title[0] + ":\n" + self.jiben)
            elif title_temp[0] == '基本解释':
                word_data_list = response.xpath(
                    '//div[@class="cont_hzcx"]/div[@class="zi_text_content"]/p[position()>1]/text()').getall()
                for item in word_data_list:
                    self.jiben += item
                self.jiben = str(self.jiben.replace('\\r\\n', '')).replace('\'', '').replace('“', '"').replace('”','"').replace('（', '(').replace('）', ')')
                print(title_temp[0] + ":\n" + self.jiben)
        except Exception as e:
            print(e)
        # 获取详细信息
        try:
            title_1 = response.xpath('//div[@class="cont_hzcx"]/div[@class="zi_text_content hide"][1]/p/b[1]/text()').getall()
            title_2 = response.xpath('//div[@class="cont_hzcx"]/div[@class="zi_text_content hide"][1]/p/b[3]/text()').getall()
            container = response.xpath('//div[@class="cont_hzcx"]/div[@class="zi_text_content hide"][1]/p//text()').getall()
            if len(title_1) != 0:
                print(title_1[0] + ':')
                self.xiangxi1 = str(container)[str(container).find('详细字义') + 6:str(container).find('词性变化')]
                self.xiangxi1 = self.xiangxi1.replace('\\r\\n', '').replace('\'', '').replace('“', '"').replace('”','"').replace('（', '(').replace('）', ')')
                print(self.xiangxi1)
            else:
                self.num = 1  # 如果没有这个字段，就改变下一个字段的起点
            if len(title_2) != 0:
                print(title_2[0] + ':')
                self.xiangxi2 = str(container)[str(container).find('词性变化') + 6:-1]
                self.xiangxi2 = self.xiangxi2.replace('\\r\\n', '').replace('\'', '').replace('“', '"').replace('”','"').replace('（', '(').replace('）', ')')
                print(self.xiangxi2)
        except Exception as e:
            print(e)
        # 获取同音字
        try:
            word_data_list_3 = response.xpath('//div[@class="list_b"]/ul/li/a/text()').getall()
            print('同音字为:')
            for it in word_data_list_3:
                self.xiangxi3 += str(it)
            print(self.xiangxi3)
        except Exception as e:
            print(e)
        # 获取汉语字典
        try:
            word_data_list_4 = response.xpath('//div[@class="cont_hzcx"]/div[@class="zi_text_content hide"][2-{}]/p[1]/text()'.format(self.num)).getall()
            print('汉语字典:')
            for it in word_data_list_4:
                self.hanyu += it
            self.hanyu = self.hanyu.replace('\\r\\n', '').replace('\'', '').replace('“','"').replace('”','"').replace('（','(').replace('）',')')
            print(self.hanyu)
        except Exception as e:
            print(e)
        item = WordsItem(name=name,pinyin=self.pinyin,bihua=self.bihua,bushou=self.bushou,jiben=self.jiben,xiangxi1=self.xiangxi1,xiangxi2=self.xiangxi2,xiangxi3=self.xiangxi3,hanyu=self.hanyu)
        yield item
