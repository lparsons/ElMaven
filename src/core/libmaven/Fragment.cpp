#include "Fragment.h"
#include "Scan.h"

using namespace std;

Fragment::Fragment() {
    precursorMz = 0;
    polarity = 1;
    scanNum = 0;
    rt = 0;
    collisionEnergy = 0;
    consensus = NULL;
    precursorCharge = 0;
}

// build fragment based on MS2 scan
Fragment::Fragment(Scan* scan,
                   float minFractionalIntensity,
                   float minSigNoiseRatio,
                   int maxFragmentSize)
{
    this->precursorMz = scan->precursorMz;
    this->collisionEnergy = scan->collisionEnergy;
    this->polarity = scan->getPolarity();
    this->sampleName = scan->sample->sampleName;
    this->scanNum = scan->scannum;
    this->precursorCharge = scan->precursorCharge;
    vector<pair<float, float>> mzarray = scan->getTopPeaks(minFractionalIntensity,
                                                           minSigNoiseRatio, 5);

    for (unsigned int j = 0; j < mzarray.size() && j < maxFragmentSize;
            j++) {
        this->mzValues.push_back(mzarray[j].second);
        this->intensityValues.push_back(mzarray[j].first);
    }
    this->obscount = vector<int>(this->mzValues.size(), 1);
    this->consensus = NULL;
    this->rt = scan->rt;
}

//make a copy of Fragment.
Fragment::Fragment(Fragment* other)
{
    this->precursorMz = other->precursorMz;
    this->polarity = other->polarity;
    this->mzValues = other->mzValues;
    this->intensityValues = other->intensityValues;
    this->obscount = other->obscount;
    this->consensus = other->consensus;
    this->scanNum = other->scanNum;
    this->sampleName = other->sampleName;
    this->collisionEnergy = other->collisionEnergy;
    this->precursorCharge= other->precursorCharge;
}

void Fragment::appendBrothers(Fragment* other)
{
    //copy other's brothers
    for(unsigned int i=0; i < other->brothers.size(); i++) {
        this->brothers.push_back( other->brothers[i]);
    }

    //empty brothers link
    other->brothers.clear();

    //append other to brother link
    this->brothers.push_back(other);
}

void Fragment::printMzList()
{ 
    for(unsigned int i = 0; i < mzValues.size(); i++ ) {
        cerr << setprecision(3) << mzValues[i] << " ";
    }
}

int Fragment::findClosestHighestIntensityPos(float mz, float tolr)
{
    float mzmin = mz - tolr;
    float mzmax = mz + tolr+0.001;

    vector<float>::iterator itr = lower_bound(mzValues.begin(), mzValues.end(), mzmin-1);
    int lb = itr - mzValues.begin();
    float highestIntensity = 0; 
    for(unsigned int k = lb; k < mzValues.size(); k++) {
        if (mzValues[k] < mzmin) continue; 
        if (mzValues[k] > mzmax) break;
        if (intensityValues[k] > highestIntensity) highestIntensity = intensityValues[k];
    }

    int bestPos = -1;
    float bestScore = 0;
    for(unsigned int k = lb; k < mzValues.size(); k++) {
        if (mzValues[k] < mzmin) continue; 
        if (mzValues[k] > mzmax) break;
        float deltaMz = (mzValues[k] - mz); 
        float alignScore = sqrt(intensityValues[k] / highestIntensity) - (deltaMz * deltaMz);
        //	cerr << mz << "\t" << k << "\t" << deltaMz << " " << alignScore << endl;
        if (bestScore < alignScore) {
            bestScore=alignScore;
            bestPos=k;
        }
    }
    //if(bestPos>=0) cerr << "best=" << bestPos << endl;
    return bestPos;
}

vector<int> Fragment::compareRanks(Fragment* a, Fragment* b, float productPpmTolr)
{ 
    bool verbose = false;
    vector<int> ranks (a->mzValues.size(), -1);	//missing value == -1
    for(unsigned int i = 0; i < a->mzValues.size(); i++) {
        for( unsigned int j = 0; j < b->mzValues.size(); j++) {
            if (mzUtils::ppmDist(a->mzValues[i], b->mzValues[j]) < productPpmTolr) {
                ranks[i] = j;
                break;
            }
        }
    }
    if (verbose) {
        cerr << " compareranks: " << a->sampleName << endl;
        for(unsigned int i = 0; i < ranks.size(); i++) {
            float mz2 = 0;
            float int2 = 0;

            if (ranks[i] >= 0) {
                mz2 = b->mzValues[ranks[i]];
                int2 = b->intensityValues[ranks[i]];
            }
            cerr << ranks[i] << "," << a->mzValues[i] << "\t" << mz2 << "\t\t";
            cerr << a->intensityValues[i] << "\t" << int2 << endl;
        }
    }
    return ranks;
}

void Fragment::addBrotherFragment(Fragment* b) { brothers.push_back(b); }

void Fragment::buildConsensus(float productPpmTolr)
{   
    if(this->consensus != NULL) {
        delete(this->consensus);
        this->consensus = NULL;
    }
    
    if (!(brothers.size() > 0)) return;

    //find brother with largest nobs
	Fragment* seed = this;
    for(Fragment* brother: brothers) {
        if (brother->nobs() > seed->nobs())
            seed = brother;
    }

    //create a copy of seed fragment
    Fragment* consensusFrag = new Fragment(seed);

    for(unsigned int i = 0; i < brothers.size(); i++) {
        consensusFrag->sortByMz();
        Fragment* brother = brothers[i];

        vector<int> ranks = compareRanks(brother, consensusFrag, productPpmTolr); 

        for(unsigned int j = 0; j < ranks.size(); j++) {
            int posA = ranks[j];	
            float mzB = brother->mzValues[j];
            float intB = brother->intensityValues[j];
            if (posA >= 0) {
                //sum intensities for m/z within ppm tolerance
                consensusFrag->intensityValues[posA] += intB;
                consensusFrag->obscount[posA] += 1;
            } else if (posA == -1) {
                //new entry if m/z does not fall within ppm tolerance of existing m/z
                consensusFrag->mzValues.push_back(mzB);
                consensusFrag->intensityValues.push_back(intB);
                consensusFrag->obscount.push_back(1);
            }
        }
    }

    //average values 
    int N = 1 + brothers.size();
    for(unsigned int i = 0; i < consensusFrag->intensityValues.size(); i++) {
        consensusFrag->intensityValues[i] /= N;
    }
    consensusFrag->sortByIntensity();
    float maxValue = consensusFrag->intensityValues[0];

    for(unsigned int i = 0; i < consensusFrag->intensityValues.size(); i++) {
        consensusFrag->intensityValues[i] = consensusFrag->intensityValues[i] / maxValue * 10000;
    }
    this->consensus = consensusFrag; 
}

vector<int> Fragment::intensityOrderDesc()
{
    int nobs = intensityValues.size();
    vector<pair<float,int>> _pairsarray(nobs);
    vector<int> position(nobs);
    for(int pos = 0; pos < nobs; pos++) {
        _pairsarray[pos] = make_pair(intensityValues[pos], pos);
    }

    //reverse sort first key [ ie intensity ]
    sort(_pairsarray.rbegin(), _pairsarray.rend());

    //return positions in order from highest to lowest intenisty
    for(unsigned int i = 0; i < _pairsarray.size(); i++) {
        position[i] = _pairsarray[i].second;
    }
    return position;
}

vector<int> Fragment::mzSortIncreasing()
{
    int nobs = mzValues.size();
    vector<pair<float, int>> pairsarray(nobs);
    vector<int> sortedPositions(nobs);
    for(int pos = 0; pos < nobs; pos++) {
        pairsarray[pos] = make_pair(mzValues[pos], pos);
    }

    //forward sort first key
    sort(pairsarray.begin(), pairsarray.end());

    //return positions for lowest to highest mz
    for(unsigned int i = 0; i < pairsarray.size(); i++) {
        sortedPositions[i] = pairsarray[i].second;
    }
    return sortedPositions;
}


void Fragment::sortByIntensity()
{ 
    vector<int> order = intensityOrderDesc();
    vector<float> a(mzValues.size());
    vector<float> b(intensityValues.size());
    vector<int> c(obscount.size());
    map<int, string> d;
    for(unsigned int i = 0; i < order.size(); i++) {
        b[i] = intensityValues[order[i]];
        a[i] = mzValues[order[i]];
        c[i] = obscount[order[i]];
        if (annotations.count(order[i]) > 0) d[i] = annotations[order[i]];
    };
    mzValues = a;
    intensityValues = b;
    obscount = c;
    annotations=d;
}	

void Fragment::sortByMz()
{ 
    vector<int> order = mzSortIncreasing();
    vector<float> tempMz(mzValues.size());
    vector<float> tempIntensity(intensityValues.size());
    vector<int> tempObscount(obscount.size());
    map<int, string> tempAnnotations;

    for(unsigned int i = 0; i < order.size(); i++) {
        tempIntensity[i] = intensityValues[order[i]];
        tempMz[i] = mzValues[order[i]];
        tempObscount[i] = obscount[order[i]];
        if (annotations.count(order[i]) > 0) tempAnnotations[i] = annotations[order[i]];
    };

    mzValues = tempMz;
    intensityValues = tempIntensity;
    obscount = tempObscount;
    annotations = tempAnnotations;
}	

void Fragment::buildConsensusAvg()
{ 
    map<float,double> mz_intensity_map;
    map<float,double> mz_bin_map;
    map<float,int> mz_count;

    vector<Fragment*> fragmentList = brothers;
    fragmentList.push_back(this);

    for(unsigned int i = 0; i < fragmentList.size(); i++) {
        Fragment* brother = fragmentList[i];
        for(unsigned int j = 0; j < brother->mzValues.size(); j++) {
            float bin = (round(brother->mzValues[j] + 0.5) * 10) / 10;
            mz_intensity_map[bin] += ((double) brother->intensityValues[j]);
            mz_bin_map[bin] += ((double)(brother->intensityValues[j]) * (brother->mzValues[j]));
            mz_count[bin]++;
        }
    }
    map<float,double>::iterator itr;
    for(itr = mz_intensity_map.begin(); itr != mz_intensity_map.end(); ++itr ) {
        float bin = (*itr).first;
        double totalIntensity = (*itr).second;
        double avgMz =  mz_bin_map[bin] / totalIntensity;
        cerr << "\t" << setprecision(3) << avgMz << " " << totalIntensity/mz_count[bin] << endl;
    }
}


double Fragment::spearmanRankCorrelation(const vector<int>& X)
{
    double d2 = 0;
    int n = X.size();
    for(unsigned int i = 0; i < X.size(); i++) {	
        if (X[i] == -1 )
            d2 += (i - n) * (i - n);	//mising values set to average distance
        else
            d2 += (i - X[i]) * (i - X[i]);
    }
    return 1.00 - (6.0 * d2) / (n * ((n * n) - 1));
}

double Fragment::fractionMatched(const vector<int>& X)
{
    if (X.size() == 0) return 0;
    int matchCount = 0;
    for(unsigned int i = 0; i < X.size(); i++) {
        if (X[i] != -1 ) matchCount++;
    }
    return ((double)matchCount / X.size());
}

bool Fragment::compPrecursorMz(const Fragment* a, const Fragment* b) {
    return a->precursorMz < b->precursorMz;
}

bool Fragment::operator<(const Fragment* b) const {
    return this->precursorMz < b->precursorMz;
}

bool Fragment::operator==(const Fragment* b) const {
    return abs(this->precursorMz - b->precursorMz) < 0.001;
}
