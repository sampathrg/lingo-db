select d_year, c_city, s_city, sum(cast(lo_revenue as int8))
as revenue
from customer_dict_encoded_str, lineorder, supplier_dict_encoded_str, date_dict_encoded_str
where lo_custkey = c_custkey
and lo_suppkey = s_suppkey
and lo_orderdate = d_datekey
and c_nation =  24 -- c_nation = 'UNITED STATES'
and s_nation = 8 -- s_nation = 'UNITED STATES'
and d_year >= 1992 and d_year <= 1997
group by c_city, s_city, d_year
-- order by d_year asc, revenue desc
