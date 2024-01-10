select d_year, c_city, s_city, sum(cast(lo_revenue as int8))
as revenue
from customer_dict_encoded_str, lineorder, supplier_dict_encoded_str, date_dict_encoded_str
where lo_custkey = c_custkey
and lo_suppkey = s_suppkey
and lo_orderdate = d_datekey
and (c_city = 123 or c_city = 202) -- (c_city='UNITED KI1' or c_city='UNITED KI5') TODO - Check if this should be 203? 
and (s_city = 114 or s_city = 191) -- (s_city='UNITED KI1' or s_city='UNITED KI5')
and d_yearmonth = 'Dec1997'
group by c_city, s_city, d_year
-- order by d_year asc, revenue desc;
