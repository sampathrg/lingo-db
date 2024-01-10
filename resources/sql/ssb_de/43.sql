select d_year, s_city, p_brand, sum(cast(lo_revenue as int8) - cast(lo_supplycost as int8)) as profit
from date_dict_encoded_str, customer_SSB_dict_encoded_str, supplier_dict_encoded_str, part_dict_encoded_str, lineorder
where lo_custkey = c_custkey
and lo_suppkey = s_suppkey
and lo_partkey = p_partkey
and lo_orderdate = d_datekey
and c_region = 2 -- This doesn't exist in the query below.
and s_nation = 8
and (d_year = 1997 or d_year = 1998)
and p_category = 3
group by d_year, s_city, p_brand
-- order by d_year, s_city, p_brand

-- select d_year, s_city, p_brand,
-- sum(lo_revenue - lo_supplycost) as profit
-- from date, customer, supplier, part, lineorder
-- where lo_custkey = c_custkey
-- and lo_suppkey = s_suppkey
-- and lo_partkey = p_partkey
-- and lo_orderdate = d_datekey
-- and s_nation = 'UNITED STATES'
-- and (d_year = 1997 or d_year = 1998)
-- and p_category = 'MFGR#14'
-- group by d_year, s_city, p_brand
-- order by d_year, s_city, p_brand;
